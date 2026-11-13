# Triple Robust Modem reception under changing HF channels

Independent literature review and implementation assessment  
Prepared 12 November 2026 for later cross-check and reconciliation  
Source revision inspected: 8f725cc2c9821cfbb51a7c81936656c30191d612

## Principal findings

The three-carrier design has a credible computational purpose: combine observations that share propagation structure, eliminate linear nuisance parameters analytically, and spend search effort on the remaining nonlinear uncertainty. The current native receiver already does part of this. It evaluates the complete three-carrier waveform jointly and integrates uncertain complex path gains with small Gaussian states. It does not enumerate every possible history of atmospheric amplitude and phase.

However, the present **HF Worst setting is not a demonstrated envelope containing every channel between clean and worst conditions**. Source inspection shows a particular second-path delay and Doppler separation, with blockwise gain evolution. Common arrival and frequency searches do not enumerate all alternative relative path delays and Dopplers. A clean signal can be represented with a vanishing second gain; a different intermediate echo geometry is not thereby represented exactly. This is a model-coverage finding, not a fresh measurement of decoding failure.

The GUI simulation is also more structured than “random atmospheric conditions” suggests. The relevant defaults use a fixed delayed ray, a fixed relative frequency rotation, and sinusoidal selective-amplitude coefficients with seeded initial phases. These effects vary in time, but their default statistical structure does not renew as a general stochastic ionospheric channel would. Additional stochastic mechanisms exist in the native implementation, yet the GUI profile examined does not enable them. Noise and oscillator phase noise are separate sources of randomness.

A control distinction matters: **Simulation = No selects physical transmission without the synthetic channel. Live/Duplex controls simultaneous reception during transmission.** In the traced live path, neither duplex state adds the selected synthetic spread or selective AM to the outgoing audio.

The literature supports compact inference over random channels, including shared and frequency-specific states, temporal basis expansions, conditional Gaussian filtering and structured delay–Doppler estimation. It also exposes limitations: acquisition is harder than established tracking; deep fades can remove usable information; poorly chosen shared states propagate errors between carriers; diffuse scattering challenges sparse-path models; and an overly flexible channel can explain an incorrect bit or noise.

Consequently, keeping GUI bounds is reasonable, but those bounds should describe a **declared channel family and computational allocation**, not one supposedly conservative simulated trajectory. The appropriate claim would specify communication reliability, false decisions, reacquisition, physical completion and complete-receiver service over that family. Successful reception for every arbitrary realization cannot be guaranteed merely by bounding individual parameters.

No implementation described below is established as the best solution. Several are plausible extensions of the existing native design; better formulations may still be found. None has been benchmarked or qualified for this modem in this investigation.

## Reading map

- [Channel definitions and bounds](#what-changing-atmospheric-conditions-mean)
- [Current implementation](#what-the-inspected-modem-does)
- [Three-carrier mathematics](#how-the-three-carriers-can-reduce-work)
- [Literature and contrary findings](#what-the-literature-supports-and-challenges)
- [Possible implementations](#possible-implementation-families)
- [Simulation and GUI semantics](#making-simulation-and-gui-bounds-scientifically-meaningful)
- [Computational constraints](#native-integration-and-computational-constraints)
- [Proposed validation](#bounded-validation-proposal)
- [Annotated bibliography](#annotated-references-and-access-limits)
- [Source fingerprints](#source-fingerprints)

## Scope and independent provenance

This review combines direct inspection of selected canonical source files with public academic papers, author manuscripts, official standards and original government research. The literature search was extended through publications discoverable on 12 November 2026. It covers the mechanisms most relevant to this receiver rather than claiming an exhaustive systematic review of all HF or multicarrier research.

Search branches covered HF channel measurement and nonstationarity; multifrequency scintillation tracking; matched-subspace detection and nuisance elimination; conditional Gaussian and particle inference; temporal basis expansions; sparse/off-grid delay–Doppler estimation; and differential or invariant alternatives. Searches combined those mechanism terms with recent years and verified titles/DOIs, then followed relevant primary references. Papers were selected for their assumptions, counterexamples and transferable structure, not merely for mentioning three carriers. Abstract-only and inaccessible results were kept separate from inspected full-text evidence.

The expressly excluded local report, /home/user/___quick/p/_cur/dataPump/pumpModem/local-evidence/robust-nonstationary-channel-research.md, was not opened or used. Neighboring recent local research outputs were not searched or consulted. No conclusions from that independent research are incorporated. Earlier public-source research generated independently in this conversation supplied leads, which were checked against primary sources for consequential claims. Local historical research summaries are deliberately not used as evidence here.

This report was originally written in `local-evidence/independent-channel-review-20261112-DyS6p6/`, covered by the repository's `/local-evidence/` ignore rule. Tracked source, tests, maintained documentation, configuration and ignore rules were unchanged by that research. At the user's request, the report was moved to `_ref/resilience-triple_Robust_Modem/Astra-6/` on 2026-11-13. Its purpose is the user's independent cross-check and reconciliation; reconciliation is the point to decide whether to retain, promote selected findings, or remove these temporary artifacts.

The repository is concurrently being edited. The named Robust files were clean relative to the inspected commit when checked; unrelated Fast, GUI-adapter and audio work was present. Source links identify observed line numbers and can drift. A source checksum appendix binds the important observations more precisely.

Evidence is distinguished as follows:

| Evidence class | Meaning here |
| --- | --- |
| Source observation | What the inspected implementation constructs or evaluates; no performance guarantee |
| Mathematical deduction | A consequence of an explicitly stated model or algebra; assumptions remain attached |
| Published result | A result in the cited paper's waveform, channel, SNR and experiment |
| Engineering proposal | An unimplemented candidate for evaluation in this modem |
| Unresolved qualification | Requires modem-specific measurements or a stronger proof |

No builds, DSP trials, performance tests or over-the-air experiments were run. Existing historical receiver benchmarks are not reinterpreted as evidence for these proposals. Published tracking or estimation results are not converted into this modem's reception probability or runtime.

## What changing atmospheric conditions mean

### A channel description that separates the effects

A useful schematic complex-baseband model is

\[
y(t)=e^{j\phi_{\mathrm{osc}}(t)}
 \int h(t,\tau)\,x_b(t-\tau)\,d\tau+n(t)+i(t).
\]

Here the transmitted alternative is \(x_b\), the propagation channel is \(h\), oscillator error is \(\phi_{\mathrm{osc}}\), receiver noise is \(n\), and interference is \(i\). For a few resolvable paths,

\[
h(t,\tau)=\sum_{\ell=1}^{K}
 a_\ell(t)e^{j\phi_\ell(t)}
 \delta(\tau-\tau_\ell(t)),
 \qquad
 \nu_\ell(t)=\frac{1}{2\pi}\frac{d\phi_\ell(t)}{dt}.
\]

These are modeling choices, not a claim that every HF channel consists of a small number of delta-function rays. A diffuse component may require a continuous delay distribution or a richer basis.

Several quantities with similar GUI language have different consequences:

| Quantity | What must be represented | Why one scalar severity is insufficient |
| --- | --- | --- |
| Common frequency offset | Oscillator error and common propagation shift | Moving all ray centers does not change their separations |
| Differential path Doppler | Different phase slopes on different paths | A common CFO search cannot independently reposition an echo |
| Doppler spectral width | Random temporal variation around a path's mean frequency | A center frequency is not a stochastic width or coherence time |
| Excess delay support | Relative arrivals of significant energy | A maximum delay is not the one delay at which an echo actually occurs |
| Delay motion | Changes in relative path timing | Needs a trajectory model or repeated local refinement |
| Path gain evolution | Correlated complex amplitude and phase changes | A random initial phase is not a renewing fading process |
| Frequency selectivity | Variation of the aggregate response across signal bandwidth | It depends on delay, path powers and interference between paths |
| Mode changes | Paths appearing, fading, splitting or disappearing | Fixed path count may cease to fit |
| Fades and interference | Temporary information loss or contamination | More search cannot reconstruct information absent from all observations |

Amplitude and phase are often coupled by propagation. Even with constant path powers, two paths produce

\[
H(f,t)=g_0(t)+g_1(t)e^{-j2\pi f\tau(t)}.
\]

Changing relative phase moves the interference pattern across frequency. The resulting amplitude notches and phase changes are not generally equivalent to independent sinusoidal AM applied to each carrier. This algebra explains why a physically shared path model can nevertheless have a frequency-selective aggregate response.

### Random realizations can have stationary statistics

A stationary stochastic model does not produce a constant channel. Its instantaneous complex gains can fluctuate continuously while their probability law and correlation function remain approximately constant over a chosen interval. The Gaussian-scatter family associated with Watterson and specified by ITU-R F.1487 uses this distinction. Fixed path delays plus stochastic gains can be a useful narrowband test model. A fixed Doppler center and a nonzero Doppler spectral width are separate parameters. [R1](https://doi.org/10.1109/TCOM.1970.1090438), [R2](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I!!PDF-E.pdf)

Conversely, real propagation can change its statistics with time, frequency, geography, modes and disturbances. The NTIA stochastic modeling work explicitly addresses limitations of narrowband stationary representations and broader delay/Doppler behavior. This supports testing more than one stationary model, without treating all structured models as physically invalid. [R3](https://its.ntia.gov/publications/download/90-255_ocr.pdf)

Randomness should therefore be modeled with temporal and cross-frequency correlation, physical support and regime changes. Redrawing unrelated amplitude, phase, frequency and delay at every sample would inject a different, often much broader spectrum. “Random” alone does not define a scientifically useful stress test.

### Bounds need a statistical meaning

A proposed receiver envelope should distinguish:

- Hard support, such as a declared maximum delay or admitted path count.
- A scale or RMS spread, which is not a strict maximum.
- Correlation time or Doppler spectral shape.
- Tail probability, outage duration and allowed regime-change behavior.
- Received energy and interference assumptions.
- The computational limits under which that coverage is offered.

Gaussian processes do not have finite amplitude support. Clipping them to enforce a hard maximum changes their distribution and spectrum. An RMS Doppler width likewise cannot be substituted for a worst instantaneous frequency excursion. The simulator and receiver should expose the distinction rather than silently using one number for all of them.

“Clean through HF Worst” is naturally a family of channels with varied parameters and trajectories. It is not necessarily a monotone ordering of difficulty. A smaller echo delay can cause a more destructive notch at the actual carrier positions; a larger coherent shift can be easier to fit than a smaller rapidly varying residual; and a weak echo may be less harmful than two similar-power paths.

## What the inspected modem does

### Simulation and physical transmission

The [console control descriptions](/home/user/___quick/p/_cur/dataPump/pumpModem/src/gui/screen_console.cpp:57) and [GUI setting mapping](/home/user/___quick/p/_cur/dataPump/pumpModem/src/gui/controller.cpp:601) distinguish Simulation from Live/Duplex.

The [simulation path](/home/user/___quick/p/_cur/dataPump/pumpModem/src/live.cpp:783) constructs a StreamingChannel and passes emitted samples through it. The [physical playback path](/home/user/___quick/p/_cur/dataPump/pumpModem/src/live.cpp:937) reads Emission samples, converts them to real PCM and sends them to audio playback. The [transmission branch](/home/user/___quick/p/_cur/dataPump/pumpModem/src/live.cpp:1204) separately handles capture suspension for nonduplex transmission and simulation-versus-live routing.

Thus the requested principle—retain receiver assumptions without adding adverse synthetic effects to a real transmission—is already consistent with this traced routing. The precise control is Simulation = No. Unchecking Live/Duplex additionally pauses live reception during the transmission.

### What HF Worst and Each 65 percent construct

The [HF preset table](/home/user/___quick/p/_cur/dataPump/pumpModem/src/tuning_presets.cpp:27) assigns HF Worst 7 ms delay and 30 Hz Doppler. In this implementation these become a relative second-ray geometry, not an RMS delay spread and random Doppler-spectrum width.

The [profile builder](/home/user/___quick/p/_cur/dataPump/pumpModem/src/robust_profile.cpp:240) multiplies spread delay and Doppler by the selected margin. With the examined default 3× margin, the values are 21 ms and 90 Hz. Both the configured receiver geometry and the simulated spread use these scaled quantities. A different margin changes this example.

[Each 65 percent parsing](/home/user/___quick/p/_cur/dataPump/pumpModem/src/robust_profile.cpp:27) sets both slow and fast selective coefficient depths to 0.65. It does not mean a single total amplitude excursion bounded to ±65 percent. The [rate selection](/home/user/___quick/p/_cur/dataPump/pumpModem/src/robust_profile.cpp:253) uses the scaled spread Doppler for the fast rate and one tenth of it for the slow rate. This example therefore selects 90 Hz and 9 Hz coefficient oscillations. These are simulator coefficient rates, not measured atmospheric distributions.

The [spread construction](/home/user/___quick/p/_cur/dataPump/pumpModem/src/channel_spread.hpp:9) uses equal direct/echo amplitudes, a seeded initial echo phase, a fixed delay and continuous fixed-rate phase rotation. The [selective perturbation implementation](/home/user/___quick/p/_cur/dataPump/pumpModem/src/channel_perturbation.hpp:89) uses dispersive branches and, by default, sinusoidal coefficient evolution.

The [native configuration types](/home/user/___quick/p/_cur/dataPump/pumpModem/include/datapump/triad_modem_v3.hpp:179) also provide optional stochastic path evolution, Doppler widths and moving-delay controls. The [GUI channel construction](/home/user/___quick/p/_cur/dataPump/pumpModem/src/robust_profile.cpp:312) leaves those path-evolution defaults disabled and leaves the selective coefficient law at Sinusoidal. Separate oscillator phase noise and additive noise remain stochastic. One streaming channel preserves state across read chunks and handles delayed output tails; this is not a per-chunk reseeding defect.

### Search and tracking are different mechanisms

The receiver searches public synchronization coordinates, including common arrival, frequency, clock and keyed epoch hypotheses. It also accumulates likelihood and channel-state evidence through time. That is a meaningful interpretation of “historical search,” but does not imply unrestricted trajectory enumeration.

The [coarse receiver](/home/user/___quick/p/_cur/dataPump/pumpModem/src/triad_receiver_coarse.cpp:289) selects one ray when spread is off and two otherwise. The [coarse path construction](/home/user/___quick/p/_cur/dataPump/pumpModem/src/triad_receiver_coarse.cpp:493) and [fine path construction](/home/user/___quick/p/_cur/dataPump/pumpModem/src/triad_receiver_fine.cpp:923) use the configured second delay and Doppler. A common coordinate moves the whole template; it does not independently change the second path's separation.

The operational fine lane is “fixed-affine” in its coordinate evolution. **Its complex gains are not constant for the whole slot.** It updates blockwise Gaussian gain states, with coherence assumptions of 0.5 s, 2 s and infinity. Typical blocks are approximately 0.25 s, assembled into approximately 0.5 s segments. A separate adaptive residual timing/rate lane exists, but [operational selection](/home/user/___quick/p/_cur/dataPump/pumpModem/src/triad_receiver.cpp:160) uses the fixed-affine result.

| Effect | Explicitly represented by current DATA model | Coverage implication |
| --- | --- | --- |
| Common arrival and frequency uncertainty | Public hypothesis coordinates | Coverage remains bounded by configured priors and admitted geometry |
| One/two configured propagation paths | Fixed relative delay/Doppler basis | Does not enumerate every intermediate relative geometry |
| Changing complex path gain | Gaussian state updates across blocks | Can absorb some mismatch, subject to temporal resolution and priors |
| Within-block arbitrary phase/frequency trajectory | No general temporal basis in the inspected model | Fast residual variation can remain mismatched |
| Selective AM depth and its slow/fast rate | Not an independent DATA likelihood search dimension | Setting a percentage is not evidence of matching every such fading pattern |
| Random Doppler width and delay-motion bounds | Affect support/band calculations and Helper handling | Wider support alone does not add DATA geometry states |
| General path birth/death or diffuse delay scattering | No general variable-order DATA path model | Requires further representation or demonstrated tolerance |
| Unknown waveform epoch/ordinal acquisition | Not established by configured-calendar operation | Must remain a separate qualification question |

Turning spread Off therefore changes the receive model; it is not merely an instruction to stop injecting simulation effects. Nevertheless, source inspection alone cannot determine which unmodeled channels are tolerated at a particular SNR. Neither “all effects are handled regardless of dropdowns” nor “every mismatch fails” is justified.

## How the three carriers can reduce work

### Actual waveform structure

For fine-cell rate \(R_f=(\mathrm{Rate}/6)\mathrm{Added}\), the carrier spacing is \(\Delta f=1.25R_f\), and nominal occupied bandwidth is \(3\Delta f\). A schematic form of the [canonical waveform](/home/user/___quick/p/_cur/dataPump/pumpModem/src/triad_modem_v3.cpp:331) is

\[
x_b(t)=\frac{E(t)}{\sqrt3}
 \sum_{c=-1}^{1}e^{j2\pi c\Delta f t}
 \sum_k a_{b,k,c}\,p(R_ft-k-\tfrac12).
\]

The [coefficient construction](/home/user/___quick/p/_cur/dataPump/pumpModem/src/triad_modem_v3.cpp:99) uses a keyed permutation of powers \(\{0.4,1,1.6\}\), independently keyed 8PSK phases and additional keyed radius/sign factors. Common normalization couples the carrier powers. Bit alternatives use independent keyed construction.

There is no encoded identity requiring the three phases to close to a prescribed bit value. Three equally spaced carriers alone do not create a phase-invariant code, a bispectrum detector, a triple-chirp probing sequence or three statistically independent fading branches.

### Shared physical paths preserve frequency selectivity

For a retained bit and synchronization hypothesis, the current model is

\[
y_n=A_{b,\theta,n}g_n+w_n.
\]

Each column is a delayed and frequency-shifted copy of the complete three-carrier signal. The [ChannelBasis contract](/home/user/___quick/p/_cur/dataPump/pumpModem/include/datapump/triad_modem_v3.hpp:343) shares a complex coefficient for each physical path across carriers. For \(K\) paths this is \(K\) complex gains, rather than \(3K\) unrelated carrier/path gains.

That reduction is physically meaningful when one path coefficient and its delay/Doppler response adequately describe the occupied bandwidth. The total response remains

\[
H(f,t)=\sum_{\ell=1}^{K}g_\ell(t)e^{-j2\pi f\tau_\ell(t)}.
\]

It can have strong frequency-selective fading even though each physical path uses a shared gain. “Shared path coefficient” must not be misread as “identical total channel on every carrier.”

The reduction is a model-dimension statement. It is not a measured threefold speedup, because template production, FFTs, memory traffic, acquisition hypotheses and scheduler costs may dominate.

### Gaussian integration summarizes channel history

The [fine sufficient statistics](/home/user/___quick/p/_cur/dataPump/pumpModem/src/triad_receiver_fine.cpp:490) are

\[
r=A^Hy/\sigma^2,\qquad G=A^HA/\sigma^2.
\]

Given \(g\sim\mathcal{CN}(m,P)\), integrating over the gain gives

\[
y\sim\mathcal{CN}(Am,\ \sigma^2I+APA^H).
\]

The [native Gaussian kernel](/home/user/___quick/p/_cur/dataPump/pumpModem/src/triad_gaussian_kernel.cpp:137) uses small square-root calculations and retains uncertainty and likelihood normalization. For nonsingular \(P\), an equivalent posterior expression is

\[
P_+=(P^{-1}+G)^{-1},\qquad
m_+=m+P_+(r-Gm).
\]

This algebra is explanatory; explicitly inverting \(P\) would not reproduce all numerical safeguards of the implementation.

The posterior mean, covariance and accumulated likelihood summarize prior blocks under the assumed state model. There is no need to store and enumerate all gain histories. Current one/two-gain states and three coherence models produce six bit/model gain posteriors per retained coordinate.

This is distinct from maximizing over gains. A least-squares fit or generalized likelihood ratio uses the best gain; Gaussian marginalization integrates possible gains with their prior probability and determinant penalty. Both can reduce dimension, but they produce different evidence, especially when the model is flexible or poorly conditioned. The coarse receiver uses a projection/Gram-based score; it should not be described as the same calculation as the fine integrated likelihood.

### What remains uncertain about exactly three

Three observations can improve conditioning or provide diversity while permitting compact shared state. The useful degree of independence depends on the channel and carrier spacing. Closely spaced carriers may share fading; widely separated carriers may require additional frequency-dependent states.

A fair modem-specific comparison must hold total energy, occupied bandwidth, bit duration, public hypotheses, false-decision criteria and complete-receiver resource constraints constant. Compare the current triad with a suitable single wideband known waveform and, if useful, other carrier counts using equally competent inference. Comparing the triad only with three independently estimated channels would confound waveform benefit with receiver architecture.

The current source and the papers below support the shared-inference principle. They do not isolate a computational or reliability advantage unique to exactly three carriers for this particular keyed waveform.

### Two useful deductions about model mismatch

First, a mismatched residual frequency \(\delta\nu\) cannot be absorbed by one constant complex coefficient over an arbitrarily long block. For a rectangular, uniformly weighted block of duration \(T\), the normalized projection of a pure residual tone onto a constant coefficient has magnitude

\[
\left|\frac{\sin(\pi\delta\nu T)}{\pi\delta\nu T}\right|.
\]

At \(T=0.25\) s, a 4 Hz residual gives a zero in this idealized calculation. Actual waveform weighting, multiple paths, frequency coordinates and overlapping processing change the result. This is not a predicted modem failure threshold. It demonstrates why inter-block gain updates cannot be assumed to repair all within-block phase rotation. A correctly modeled 90 Hz path center is already removed by its template and is not a 90 Hz residual.

Second, let \(\mathcal S_b\) be the family of waveforms that bit \(b\) can generate after the admitted channel transformations. If \(\mathcal S_0\) and \(\mathcal S_1\) substantially overlap, fitting more channel freedom can reduce bit distinguishability. In the extreme, an unconstrained per-sample gain can explain either bit wherever its template is nonzero. Priors, temporal smoothness, shared structure and calibrated null comparisons are therefore part of detection, not merely computational conveniences. This is a deduction about model families, not proof that the current bit alternatives overlap harmfully.

## What the literature supports and challenges

### HF propagation measurements and model scope

The foundational Watterson work established a useful statistical HF model from measurements; F.1487 subsequently specified a practical simulator family for bandwidths up to approximately 12 kHz. This is a defensible baseline, not a complete description of every disturbed, wideband or nonstationary channel. [R1](https://doi.org/10.1109/TCOM.1970.1090438), [R2](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I!!PDF-E.pdf)

Nissen and Bello's disturbed wideband measurements distinguish Doppler shift, Doppler spread and frequency correlation. Their Greenland–Massachusetts campaign and selected propagation modes constrain applicability; processing bandwidth also matters to the measured correlation. This supports measuring the relevant channel over the modem's occupied bandwidth instead of assuming a universal coherence bandwidth. [R4](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2002RS002746)

Dear and colleagues' 2024 Java NVIS study supplies a contrasting low-latitude, geographically limited measurement example. Its reported delay and Doppler behavior comes from a particular link and observation period, not a global “worst” envelope. In particular, Doppler shift values must not be relabeled Doppler spectral widths. [R5](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2023RS007877)

Fernandes and colleagues, published 11 March 2026, analyze frequency residuals from a 10 MHz WWV link in five-minute windows under geomagnetically quiet conditions. Different fitted distributions appear in different daily regimes, including Cauchy-like daytime residuals. This strengthens the case for regime-aware tests. It does **not** establish that individual channel gains are Cauchy distributed, supply a per-path Doppler power spectrum, or refute a short-time Gaussian-scatter model. Frequency-residual histograms and complex-channel correlation are different observables. [R6](https://www.frontiersin.org/journals/astronomy-and-space-sciences/articles/10.3389/fspas.2026.1713968/full)

The July 2026 Alaskan beacon study is especially relevant to interpretation of measured spread: it explicitly considers whether changes in delay and Doppler during a one-minute processing interval could contribute to apparent spreading. Observational windows can mix physical scattering with secular motion. A simulator intended to reproduce one of these measurements must reproduce the appropriate timescale, not just copy a scalar width. [R7](https://www.frontiersin.org/journals/astronomy-and-space-sciences/articles/10.3389/fspas.2026.1848834/full)

Xu and colleagues' April 2026 geolocation study offers an accessible example of combining a regional ionospheric background with correlated synthetic perturbations and an ITS channel model. Its minute-scale background-parameter evolution is not evidence that fast channel coefficients remain coherent for minutes. Statistical agreement with earlier soundings and a field geolocation test do not constitute event-matched validation of every simulated channel trajectory. [R29](https://www.mdpi.com/1424-8220/26/9/2755)

These studies support a hierarchy: simple controlled channels for diagnosis; stationary stochastic channels for repeatable statistics; nonstationary mode and spectrum changes for robustness; and measured traces for external validity. They do not justify one universal random process for all HF paths.

### Recent HF tracking demonstrations

Ji and Wu's 2025 MATE paper combines modulation tracking with multipath delay estimation. It includes Watterson simulations and a real HF link, so it should not be dismissed as simulation-only evidence. Its stated bandwidth and resolvable-path conditions limit transfer to other waveforms; its delay-tracking result does not establish weak-signal acquisition or bit/null discrimination for Robust. [R8](https://www.mdpi.com/1424-8220/25/14/4246)

Their March 2026 MSIT paper separates paths and tracks them independently using carrier and delay loops. MUSIC serves initialization rather than continuous full processing. The field example reports 43 dB in-band SNR after filtering, while the cited simulation setup uses 20 dB SNR. This is useful evidence that structured path separation and tracking can work on an HF link. It is not evidence that the same method works at this modem's weakest operating point or within its total search budget. Initialization, matrix solves, failures and reacquisition must be charged. [R9](https://www.mdpi.com/1424-8220/26/5/1640)

The constructive lesson is to distinguish a broad acquisition stage from a compact tracking stage. The contrary lesson is that strong-signal path metrology cannot stand in for low-SNR message reception.

### Multifrequency tracking under scintillation

Vilà-Valls, Closas and Curran's 2017 GNSS work jointly models line-of-sight dynamics and frequency-specific scintillation using a nonlinear Kalman formulation. It uses L1/L2/L5 observations and synthetic scintillation. Its signal model assumes timing synchronization after acquisition. This directly supports exploiting shared structure while retaining distinct disturbance states, but not this modem's blind acquisition or HF-reflection geometry. [R16](https://new.eurasip.org/Proceedings/Eusipco/Eusipco2017/papers/1570339391.pdf)

Wang and colleagues' 2023 cross-frequency Doppler assistance analysis is an important counterweight. Noise from the assisting loop contributes to the aided estimate; improvement has a stated equivalent-noise-bandwidth condition, and excessively narrow tracking bandwidth impairs dynamic response. Their approximately 4 dB experimental threshold improvement is specific to BeiDou pilot assistance and preliminary field tests, not a general computational saving or a turbulence guarantee. [R17](https://link.springer.com/article/10.1007/s10291-023-01434-4)

Sun and colleagues' 2024 BDS work uses shared propagation and clock structure with ionospheric and per-frequency ambiguity states. Its highlighted 22 dB-Hz weak-signal experiment uses five frequencies, and the results are simulations; real-signal evaluation is identified as future work. It supports joint estimation as a design direction, without proving an advantage specific to three frequencies or reporting complete-modem execution cost. [R18](https://www.mdpi.com/2072-4292/16/23/4352)

For triple Robust Modem, the transferable principle is **shared physical state plus justified residual state and reliability treatment**. GNSS carriers are widely separated and traverse the ionosphere differently from an HF skywave. Their numerical gains cannot be imported.

### Eliminating nuisance dimensions and retaining uncertainty

Matched-subspace detection shows how unknown linear coefficients can be eliminated under an explicit Gaussian observation/subspace model. Scharf and Friedlander's optimality results belong to their invariant test classes and assumptions; they do not imply optimality for arbitrary unknown atmospheric channels. Their formulation also makes clear that nearby competing subspaces are hard to distinguish. [R10](https://www.mssc.mu.edu/~daniel/mscs6960/spring2010/ScharfFriedlanderTSP94.pdf)

Li and colleagues' April 2026 OFDM method analytically eliminates the channel vector by least squares, leaving a timing/CFO search and reusable computations. This is a recent example of avoiding a naive joint grid. It is profiling, not Bayesian gain integration. The remaining fixed-step grid still controls cost, and the authors leave highly dynamic channels and complex interference for further validation. [R14](https://www.mdpi.com/2673-4001/7/2/45)

Kim, Pun and Iltis provide a conditional-state alternative: sample nonlinear frequency states while updating Gaussian channel states analytically. Their Rao-Blackwellized particle receiver is approximate, includes linearization and a suboptimal multiuser decomposition, and exhibits a particle-count accuracy/cost tradeoff. It is evidence for an inference structure, not a guarantee that a few particles cover a broad low-SNR acquisition distribution. [R13](https://www.merl.com/publications/docs/TR2010-095.pdf)

Kislal and colleagues' finite-blocklength synchronization analysis adds a different caution: conclusions depend on how timing uncertainty is shared between observations. Their favorable case with dependent delays does not carry unchanged to independent delays. Joint channel estimation cannot make synchronization overhead irrelevant in every setting. [R15](https://research.chalmers.se/publication/543021/file/543021_Fulltext.pdf)

### Temporal bases and diffuse Doppler

Zemen and Mecklenbräuker's DPSS approach represents finite-time, approximately Doppler-limited variation with a small temporal basis. Significant dimension scales with the time–bandwidth product; mode truncation trades bias against estimation variance. Its OFDM simulations and small-intercarrier-interference assumptions are not HF receiver qualification. Wang and Lim's order-selection abstract further cautions that observation-window leakage and estimation error matter, beyond nominal Doppler width alone. [R11](https://thomaszemen.org/papers/Zemen05-IEEETrSignalProcessing.pdf), [R12](https://www.jstage.jst.go.jp/article/transcom/E91.B/6/E91.B_6_2037/_article/-char/en)

Qu and colleagues explicitly study continuous Doppler spread in OTFS, using a low-dimensional transform representation rather than assuming that every scatterer is an isolated discrete Doppler point. Their pilot pattern and detector are OTFS-specific. The pertinent contradiction is to the claim that every doubly selective channel is adequately sparse on a fine delay–Doppler grid. [R19](https://eprints.gla.ac.uk/238160/1/238160.pdf)

Benzine and colleagues' work published online in 2025 combines double sparsity with shifted DPSS bases for off-grid Doppler. Its error guarantees and dimension reduction depend on the stated sparse model and support assumptions; higher basis order also increases unknowns. This is a valuable hybrid between sparse geometry and compact continuous variation, not a theorem covering arbitrary diffuse HF propagation. [R22](https://arxiv.org/abs/2504.13762v2)

Liu and colleagues' AFDM paper, published online in May 2026 after a 2025 preprint, uses a generalized complex-exponential basis to reduce time-varying channel estimation to fewer coefficients and address fractional Doppler. It supports compact representation under a waveform-specific training design. It does not imply that changing Robust's modulation to AFDM would reduce complete receiver cost. [R21](https://arxiv.org/html/2504.18901v2)

Recent abstract-only work on semi-blind DPS-BEM/particle filtering and two-dimensional DPS-BEM for MIMO OTFS indicates continuing development. Because full methodological access was unavailable, it is retained as a lead rather than the basis for an implementation choice. [R27](https://doi.org/10.1016/j.phycom.2025.102874), [R28](https://ieeexplore.ieee.org/document/11165134/)

### Sparse geometry and specialized waveform design

Khan and Mohammed's fractional-delay/Doppler OTFS method separates path estimation and, under sufficiently fine resolution, delay and Doppler estimation. Its lower-cost two-step approximation has conditions; practical pulse shapes, fractional offsets and insufficient resolution undermine simple sparsity and decoupling. This motivates local refinement, not indiscriminate replacement of a joint likelihood by independent one-dimensional searches. [R20](https://arxiv.org/html/2111.06009v1)

Li and colleagues' 2022 belief-propagation method addresses time-varying path count and association. It uses a snapshot parametric estimator to produce noisy path measurements, then performs sequential probabilistic association and estimation. That architecture is relevant to birth/death, but its measurement front end, particle costs and demonstrated conditions do not establish viable weak-signal bit acquisition here. [R24](https://arxiv.org/abs/2109.05623)

Fish and Gurevich's special probing sequences show that waveform design can genuinely change search complexity. Their sparse discrete delay–Doppler model gives approximately \(O(N\log N+r^3)\) for a triple-chirp incidence method and \(O(N\log N+r^2)\) for a double-chirp cross method, with genericity assumptions. “Triple-chirp” means an ambiguity-structured probing sequence, not the modem's three independently keyed carriers. The result is a research alternative, not a property already present in this waveform. [R23](https://arxiv.org/html/1309.6036v1)

### Differential and invariant alternatives

For equally spaced frequencies, a phase response affine in frequency is canceled by weights \([1,-2,1]\). The corresponding complex product is \(z_{-1}z_{+1}(z_0^*)^2\): it is fourth degree, although it uses three carriers. This is a direct algebraic observation. It only helps detection if the transmitted alternatives preserve discriminating information in that invariant and the channel follows the canceled phase law. General multipath does not.

Products and ratios of noisy observations can lose useful amplitude information, amplify near-zero behavior and require different noise models. Differential communication literature analyzes noisy-reference penalties; bispectrum inversion literature likewise shows demanding high-noise sample requirements in its repeated-observation setting. Neither furnishes a generic low-cost, low-SNR replacement for the current complex likelihood. [R25](https://par.nsf.gov/servlets/purl/10059425), [R26](https://adachifumiyuki.jp/wp-content/pdf/publish_2006/06_pu_2006_liu.pdf)

The contrary result also matters: Liu and Adachi's decision-feedback MMSE differential scheme outperforms its practical pilot-assisted coherent MMSE comparison in simulated large-Doppler cases. Estimation/tracking mismatch can reverse a coherent receiver's advantage, but this result belongs to its differential DS-CDMA waveform and uncoded single-user simulations, not current Robust. [R26](https://adachifumiyuki.jp/wp-content/pdf/publish_2006/06_pu_2006_liu.pdf)

A small noncoherent or invariant stage could still be useful for proposing candidates under severe common-phase uncertainty. It would need a modem-specific discrimination and false-alarm comparison, including cases where phase invariance helps and where its information loss dominates.

## Possible implementation families

The following are engineering proposals. Their ordering reflects proximity to the present code and ease of obtaining discriminating evidence, not a ranking of ultimate performance. A hybrid may be preferable to any one method.

### Preserve the current shared Gaussian core

The smallest conceptual change is to retain the joint three-carrier sufficient statistics and square-root gain integration while making the admitted channel model explicit. This preserves the existing advantage of treating linear gain uncertainty analytically.

First expose which quantities are shared, fixed, searched, or stochastic. Separate the simulation configuration from the receive envelope even if the GUI initially derives both from one preset. A receiver configured for a broad envelope should be tested on independent truths throughout it, rather than given the simulator's exact delay/Doppler pair.

The cheapest falsifying check is a deterministic mismatch matrix: keep the receiver envelope fixed while changing one true echo delay, differential Doppler or path power at a time, including off-grid/intermediate values and no echo. This identifies whether a new representation is needed before paying for a large stochastic experiment.

### Add a small temporal basis within each path

A candidate extension is

\[
g_\ell(t)\approx\sum_{q=0}^{Q-1}c_{\ell q}u_q(t),
\]

where \(u_q\) are finite-window temporal basis functions. Each path column becomes a small collection of waveform-times-basis columns. The coefficient vector remains linear, so Gaussian integration can be retained. DPSS, complex exponentials, low-order local polynomials and covariance eigenvectors are alternatives with different approximation properties. The DPSS literature provides one principled choice when a credible Doppler bandwidth is available. [R11](https://thomaszemen.org/papers/Zemen05-IEEETrSignalProcessing.pdf), [R19](https://eprints.gla.ac.uk/238160/1/238160.pdf), [R22](https://arxiv.org/abs/2504.13762v2)

With \(K\) paths and \(Q\) modes per path, the state dimension becomes \(d=KQ\), before adding residual states. A dense generic sufficient-statistic construction over \(M\) samples can cost \(O(Md^2)\), with small dense algebra up to \(O(d^3)\) and covariance storage \(O(d^2)\). Known structure, reused Gram matrices, orthogonal bases and batched projections may lower actual work; those gains require measurement.

For an approximately bandlimited process supported in \([-B,B]\) over duration \(T\), significant temporal dimension is roughly \(2BT\), plus modes needed for the chosen approximation error. This is a scale estimate, not an exact universal rank prescription. A hypothetical \(B=30\) Hz over \(T=0.25\) s already gives a time–bandwidth product of 15. A “small basis” is therefore conditional on the residual bandwidth, block length and tolerated error. The current configured 90 Hz ray-center separation must not be substituted for \(B\).

Risks include basis leakage, incorrect covariance, ill conditioning, increased null/incorrect-bit fit and model-order sensitivity. A basis models temporal variation at the admitted delay; it does not by itself move that path's delay. Retaining the existing inter-block transition is complementary to modeling within-block variation.

**First discriminator:** compare a few predeclared basis orders on controlled within-block residual rotation and independently shaped fading, while holding geometry and all public hypotheses fixed. Measure correct-bit versus wrong-bit/null separation as well as approximation error and total work.

### Search a bounded set of geometry models and refine locally

A second candidate is a small bank of relative-delay/Doppler configurations, each retaining analytical gain integration. It can include the clean model, interior geometries and boundary geometries. Local refinement can use waveform derivatives or interpolation around promising states.

As a local approximation,

\[
x(t-\tau-\delta\tau)\approx x(t-\tau)-\delta\tau\,x'(t-\tau).
\]

This can reuse template derivatives, but the physically constrained coefficients contain products such as \(g\,\delta\tau\). Treating derivative coefficients as completely independent is a relaxed model and may fit wrong signals. Large offsets, path crossings or deep fades can invalidate local linearization.

Grid spacing should be justified by waveform ambiguity and model mismatch, with fractional offsets tested between grid points. Delay sensitivity typically depends on occupied bandwidth, and residual Doppler sensitivity on observation duration. Neither suggests uniformly refining every parameter independently to extreme precision.

If \(J\) geometry states are retained, cost can grow approximately with \(J\) unless projections or sufficient statistics are shared. Beam search, branch-and-bound, derivative proposals or hierarchical tiling may reduce work, but pruning can discard the correct weak path. A cheap stage should propose or order work; it must not silently remove required public hypotheses or replace unperformed coverage with a confidence claim.

Model evidence also needs consistent priors or a calibrated maximum-score correction. Adding models and selecting the highest score can raise false admissions even if each individual model is sound.

**First discriminator:** inject paths midway between bank points and near two indistinguishable local maxima. Check whether the correct bit survives proposals, refinement, scoring and null calibration under the same work limit.

### Track nonlinear geometry while integrating linear gains

A conditional-state formulation separates geometry \(z_n\) from gains \(g_n\):

\[
p(z_n,g_n\mid y_{1:n})
 \approx\sum_{j=1}^{J}w_n^{(j)}
 \delta(z_n-z_n^{(j)})
 \mathcal{CN}(g_n;m_n^{(j)},P_n^{(j)}).
\]

Possible outer representations include a discrete mixture, sigma-point approximation, local EKF/UKF, or a Rao-Blackwellized particle filter. The inner conditional Gaussian update reuses the current mathematical structure. Nonlinear state might include relative delay, differential Doppler, their rates, or a mode identifier. [R13](https://www.merl.com/publications/docs/TR2010-095.pdf)

A frequency-center state and the phase slope of a free complex gain can describe the same change. The parameterization needs a convention or prior that separates center motion from residual fading; otherwise added states can be redundant and poorly conditioned. This is an identifiability issue, not extra channel information.

A local Gaussian tracker can be economical after acquisition but cannot safely summarize every multimodal acquisition ambiguity. Particles handle multimodality more naturally but require enough support; resampling, impoverishment and rare transitions matter. Process noise that is too small locks onto the wrong geometry; too much noise wastes information and computation.

A practical architecture could use broad bounded acquisition, compact tracking when supported by observations, and explicit reacquisition after evidence deteriorates. A tracker initialized at the true state is only an oracle diagnostic; it does not qualify the acquisition path.

**First discriminator:** start with an incorrect or ambiguous geometry, introduce a fade plus a mode change, then restore signal energy. Measure recovery, erroneous persistence, wrong-bit decisions and extra service work—not only steady-state tracking RMSE.

### Combine sparse resolved paths with a diffuse component

Where observations support a few strong modes plus diffuse scattering, a hybrid can use explicit path geometry for the resolved part and a low-rank covariance or basis for the diffuse part:

\[
y=A_{\mathrm{resolved}}g+v,\qquad
\operatorname{Cov}(v)=R_n+A_{\mathrm{diffuse}}Q_dA_{\mathrm{diffuse}}^H.
\]

This is a proposed approximation. It avoids pretending every diffuse contribution is one sharply localized path. Low-rank covariance identities can retain manageable solves if the diffuse rank is small.

The danger is double-counting the same uncertainty in explicit paths and residual covariance, or making residual covariance so broad that it hides bit evidence. If the covariance depends on the candidate waveform, its determinant and effect on null calibration must be retained. A scalar noise inflation cannot generally reproduce structured signal-dependent distortion.

Sparse Bayesian learning, message passing and off-grid estimators are alternatives for discovering resolved paths, with the limitations identified in the literature. They need not be inserted wholesale into the hot path. [R19](https://eprints.gla.ac.uk/238160/1/238160.pdf), [R20](https://arxiv.org/html/2111.06009v1), [R24](https://arxiv.org/abs/2109.05623)

**First discriminator:** compare a few-ray channel and a diffuse channel with similar measured RMS spreads. If one algorithm's apparent advantage vanishes outside its own sparse simulator, it has not qualified the intended envelope.

### Add limited carrier-specific residual states

When the shared path model leaves structured discrepancies across carrier bands, a hierarchical model might combine shared gains with shrinkage-controlled residuals:

\[
g_{\ell,c}(t)=g_\ell(t)+\epsilon_{\ell,c}(t).
\]

The shared term keeps joint information; residual covariance expresses how much departures are allowed and how they correlate. A constraint or hierarchical prior is needed to separate common and residual components. An alternative is a low-rank frequency basis instead of three unconstrained gains.

This is consistent with the multifrequency literature's distinction between shared motion and separate disturbances. It does not require assuming independent fading on three nearby bands. [R16](https://new.eurasip.org/Proceedings/Eusipco/Eusipco2017/papers/1570339391.pdf), [R17](https://link.springer.com/article/10.1007/s10291-023-01434-4), [R18](https://www.mdpi.com/2072-4292/16/23/4352)

The cost is more state and potentially weaker bit discrimination. Reliability weighting must be based on a justified noise/distortion model; simply down-weighting every low-amplitude sample can discard real faded-signal evidence or bias the score.

**First discriminator:** introduce a moving notch across the three bands and a narrowband interferer in one band. Compare the shared-only, fully independent and limited-residual models at equal false-decision rates and total energy.

### Compare shorter blocks with richer blocks

Shorter blocks reduce within-block model error but provide less coherent information per gain estimate and more updates. Longer blocks improve energy accumulation only when the temporal model remains adequate. Temporal basis order, block duration and transition dynamics should therefore be considered jointly.

A bank of a few predeclared timescales may be more reliable than selecting one timescale from a noisy early estimate. Adaptive selection can be useful, but the selection cost, uncertainty and occasional wrong decision belong in both communication and service accounting.

**First discriminator:** compare fixed short blocks, longer blocks with a few temporal modes, and the current block model over the same family of trajectories. Do not judge the choice using approximation error alone.

### Revisit waveform structure only as a separate design study

Special probing sequences, explicit pilot structure, differential relationships, chirp-based modulation or other carrier counts could reduce nonlinear search. These changes affect protocol compatibility, spectral geometry, keyed addressing and data distinguishability. The Fish–Gurevich results establish that sequence design can matter, but do not designate a replacement for this modem. [R23](https://arxiv.org/html/1309.6036v1)

A fair investigation would first calculate ambiguity and competing-bit/channel subspace separation for candidate waveforms, then compare complete reception under matched constraints. It should preserve the current canonical implementation until there is an authorized, evidence-backed protocol decision.

Learned estimators could eventually help propose states or adapt priors. They would need independent channel distributions, out-of-distribution and false-admission evaluation, and deterministic resource accounting. Recent ionospheric forecasting work is not evidence that a neural receiver will solve this problem. No learned method is selected by this review.

## Making simulation and GUI bounds scientifically meaningful

A defensible separation is:

| Concept | Proposed role |
| --- | --- |
| Receiver envelope | Admitted delay support, differential Doppler, residual dynamics, model order and resource limit |
| Simulation realization | A channel drawn within or deliberately outside that envelope, with independent seeds |
| Physical output selection | Whether emitted samples pass through a synthetic channel or go directly to the audio device |
| Duplex selection | Whether capture remains active during physical transmission |
| Qualification label | The channel family, SNR/interference assumptions and evidence supporting operation |

Existing preset labels could remain convenient summaries while a detailed view states their actual meaning. Changing their semantics requires an implementation decision; this report does not make that change.

### Candidate simulation hierarchy

1. **Exact clean baseline.** No channel impairment; preserve a true Off case rather than a nearly clean random approximation.
2. **Deterministic diagnostic paths.** Fixed or deliberately moving delay and phase; controlled notches; known boundary and interior values. These isolate causal failures.
3. **Stationary stochastic multipath.** Complex correlated gains with independently specified path powers, mean delays, Doppler centers and spectra. Gaussian-scatter is one baseline, not the entire family.
4. **Nonstationary regimes.** Correlation, path power, mean Doppler, delay and mode count evolve over declared timescales, including smooth transitions and separately justified abrupt events.
5. **Independent measured or independently generated channels.** Use known measurement bandwidth and time resolution; preserve unavailable or censored fades. Avoid using the same approximate basis to generate and receive every trial.

A possible stochastic gain construction is a specular component plus filtered complex Gaussian scattering, with cross-path or cross-frequency covariance specified where justified. The selected spectrum determines temporal correlation. A complex Ornstein–Uhlenbeck process is easy to update recursively but has exponential correlation and a Lorentzian spectrum; it is not automatically a Gaussian-Doppler Watterson process. A Gaussian spectral shape also has tails rather than strict finite support. Any truncation, filtering or bounded transform changes the model and should be recorded.

Moving delays require continuous fractional-delay handling and preserved tails. Evolving phase should be integrated from frequency, rather than resetting phase when a frequency parameter changes. Gain, delay and frequency trajectories should remain continuous across processing chunks unless an explicitly modeled event requires otherwise.

A percentage such as Each 65 cannot by itself specify Rayleigh/Rician fading, a scintillation index, fade duration, frequency correlation or Doppler spectrum. If retained as a deterministic stress control, it should be described that way. If mapped to a stochastic model, the mapping needs an explicit definition and independent validation.

### Avoid circular validation

A receiver that exactly matches its simulator can pass because both omit the same physics. Use separate parameter generation, fractional offsets between grids, different spectral shapes, off-model path counts and measured traces where possible. Separate truth from receiver configuration in the harness. Never provide the receiver realized gains, trajectory, exact path locations or future state except in explicitly labeled oracle diagnostics.

Longer traces are not automatically more independent evidence. Correlated windows from one realization do not replace independent seeds or independent propagation conditions. Delayed energy must remain in the trial through quiet slots, because it can affect both data decisions and physical message completion.

## Native integration and computational constraints

The plausible near-term candidates extend the existing C++ stack. Python may orchestrate experiments and analyze outputs; it should not become the operational DSP or persistent-state owner.

Natural integration boundaries are the channel configuration, basis generation, coarse proposals, fine sufficient statistics, Gaussian state kernel, continuous receiver orchestration and streaming simulation. They should share immutable samples and reuse templates or transforms where mathematically valid. The current files linked above identify these boundaries without prescribing a patch.

A preliminary cost model should include, for each public coordinate, bit alternative, coherence/model choice and admitted owner:

\[
\text{work}\approx
\text{preparation}+\text{projections}+\text{Gram work}
+\text{state updates}+\text{proposal/refinement}
+\text{commit and codec work}.
\]

Increasing \(K\), \(Q\), geometry count or owner count affects both scratch and retained state. A small matrix dimension in isolation does not establish a cheap receiver. Unsuccessful Helper acquisition and broad searches can dominate; they must remain in complete-service measurements.

Preserve the existing full software budget \(W=2H\), with \(H\) the effective hardware-thread capacity after explicit allocation and OS limits. Smaller useful stage dispatch must not reduce that global ceiling. Respect exact owner and memory bounds, input assembly/storage, worker stacks, retained out-of-order results and full DSP workspace throughout ownership.

For GPU work, batch compatible projections and matrix operations only when preparation, transfer and memory effects justify it. Suitable lower precision may increase the challenging-case ceiling, but cancellation-sensitive likelihoods and state conditioning require separate evidence. FP16 storage, FP16 arithmetic and matrix acceleration are different claims. A tiny Gaussian solve may remain cheaper on the CPU even when bulk projections benefit from a GPU.

No speedup, available headroom or GPU/CPU capacity is estimated here. Such claims require complete same-stream service measurements on the native implementation and representative hardware, with exact executed arithmetic and fallback reporting. A standalone kernel speedup or an easy successful slot is insufficient.

## Bounded validation proposal

This section specifies future evidence, not tests run or a new authorization to start a campaign. Use the project's hierarchical, computationally minimal method: cheap deterministic checks first, then a predeclared small statistical sample, and expand only when the decision remains unresolved.

### Separate the questions

| Question | Suitable evidence | Evidence that does not answer it |
| --- | --- | --- |
| Does the model represent the signal? | Residual/projection error and conditioning under known truth | A decoded easy example |
| Does the detector distinguish bits and nulls? | Complete scored reception with wrong alternatives and noise/interference controls | Channel-estimate RMSE alone |
| Does acquisition find the state? | Unknown initial states within declared public hypotheses | Tracking initialized at truth |
| Does tracking survive changes? | Fades, transitions, path crossings and recovery | Static matched-channel success |
| Does physical completion work? | Delayed tails, interior erasures, two scored quiet slots | Transport EOF or a known message length |
| Can the receiver keep up? | Complete native service, backlog and workspace measurements | Parallel independent simulations or one fast kernel |

### Minimum discriminating channel matrix

Predeclare a manageable selection from the following strata rather than taking their exhaustive Cartesian product:

- Clean and selected effects truly Off.
- Matched fixed geometry as a diagnostic control.
- Interior delays and differential Dopplers with the receiver bound held at HF Worst.
- Fractional offsets between any chosen grid points.
- Direct/echo power imbalance, equal-power cancellation and moving notches.
- Slowly changing and rapidly changing residual gain at the same nominal spread.
- Smooth frequency drift, random correlated phase evolution and a frequency-regime change.
- Fixed delay versus moving delay with comparable total support.
- A few resolved paths versus diffuse scattering with comparable reported RMS spreads.
- Path birth/death, crossing or temporary disappearance.
- One impaired carrier band and correlated fading of all three.
- Low received information, narrowband interference and noise-only input.
- Impaired truth while the receiver is configured Off, recorded as a mismatch/tolerance test rather than expected qualified coverage.
- Tail energy and an interior gap followed by resumed transmission.
- Acquisition failure, false candidate retention, reacquisition and final quiet-slot scoring.
- Unknown address/epoch/ordinal scope when that broader acquisition is actually claimed; configured-calendar tests must keep their narrower scope explicit.

A desired range from ideal to near maximum should include trajectories that traverse several regimes within one transmission. Independent static trials at a few severities do not establish this behavior.

### Communication statistics

Specify the target error/erasure/false-admission criteria before selecting sample counts. Report confidence intervals and unresolved outcomes. Paired seeds can reduce variance when comparing algorithms, but they are not independent evidence for absolute reliability.

Do not infer rare-event reliability from a handful of successful runs. For illustration, with zero failures in \(n\) independent Bernoulli trials, the one-sided 95 percent upper failure-probability bound is \(1-0.05^{1/n}\), approximately \(3/n\) for large \(n\). This is a statistical calculation, not a proposed project threshold. Dependence between trials or post hoc selection invalidates the simple interpretation.

Use early stopping for clearly inferior methods and explicit uncertainty-based stopping for close comparisons. Keep communication trials separate from timing replicas. A probabilistic model's calibrated bit/null evidence matters as much as its average decoded-bit rate.

### Complete service and workspace

Measure sustained productive work on one input stream, with startup/fill/drain separated from declared backlog intervals. Include recurring preparation, fine search, unsuccessful candidates, gain/basis updates, model selection, quiet-slot completion, reacquisition, codec work and orchestration. Charge memory while it is owned, including completed results awaiting ordered commit.

Compare the current source and candidate with identical public hypotheses and declared resources. If the candidate cannot cover the same family within the budget, report that directly. A smaller hypothesis bank is a changed coverage contract, not a free optimization.

Use a brief nominal fine session and separate conservative-state work where required by the existing capacity contract. Keep local CPU capacity, actual GPU execution and forecasts distinct. Do not run exhaustive or multi-day sweeps to compensate for an unclear model or statistical question.

### Establishing the contribution of the three carriers

At equal energy, bandwidth and duration, measure:

- Competing-bit/channel separation and conditioning.
- Acquisition probability and false decisions across the same channel family.
- Sensitivity to shared versus carrier-specific distortion.
- Complete work, workspace and worst backlog behavior.
- Benefit from joint inference separately from benefit from the carrier count.

This isolates the user's stated design purpose. It leaves open the possibility that three carriers are a particularly useful compromise, that most benefit comes from the shared inference architecture, or that another waveform offers a better tradeoff.

## Conclusions for the requested operating behavior

The desired behavior is technically coherent: bound the permitted environmental uncertainty in the GUI, transmit the clean canonical waveform over real audio, and infer the changing channel from received history with as much shared structure as the three carriers provide.

The present implementation satisfies only part of that description. It already uses compact joint gain inference and does not inject the synthetic channel into physical playback. Its configured path geometry and within-block model do not establish coverage of all intermediate, randomly evolving channels suggested by HF Worst and Each 65 percent.

The most useful next engineering decision is which missing uncertainty dominates at the intended operating point: residual within-block variation, relative delay/Doppler mismatch, diffuse scattering, mode changes or carrier-specific residual distortion. A small set of independent discriminators can answer that before selecting a larger algorithm.

Temporal bases, bounded geometry mixtures, conditional nonlinear tracking and shared-plus-residual models are credible candidates. Sparse methods and waveform redesign may also merit reconsideration under the right channel assumptions. None should be promoted as the definitive implementation until it preserves bit/null discrimination, required public hypotheses, physical completion and the complete-receiver computational ceiling.

The unresolved modem-specific questions are the reliability achieved over a declared nonstationary family, the total cost of covering that family, and the advantage attributable specifically to three carriers. The academic literature narrows the choices and exposes misleading shortcuts; it does not settle those questions by itself.

## Annotated references and access limits

The links below point to publishers, authors, their institutions or official repositories. “Full text” means the relevant primary text was inspected during this independent investigation, including its preceding public-source research. It does not imply replication of the paper. Page and equation locators refer to the linked version, especially where an author manuscript differs from the final publication. Abstract-only leads are explicitly identified.

### HF propagation and measurements

**R1.** C. C. Watterson, J. R. Juroshek and W. D. Bensema, *Experimental Confirmation of an HF Channel Model*, IEEE Transactions on Communication Technology, 1970. [DOI 10.1109/TCOM.1970.1090438](https://doi.org/10.1109/TCOM.1970.1090438). Historical primary citation; original full text was not retrieved. The operational model description in this review is supported by the inspected official R2 and original modeling discussion in R3, not an asserted fresh reanalysis of R1's data.

**R2.** ITU-R F.1487, *Testing of HF modems with bandwidths of up to about 12 kHz using ionospheric channel simulators*, approved May 2000; later editorial amendments noted in the document. [Official full text](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I!!PDF-E.pdf). Decisive locators: Annex 1 §§1–3, equations (1)–(2); Annexes 2–3 distinguish comparative and quantitative testing. The model uses random tap gains and separates spectral centers from widths.

**R3.** L. E. Vogler and J. A. Hoffmeyer, *A New Approach to HF Channel Modeling and Simulation, Part II: Stochastic Model*, NTIA Report 90-255, February 1990. [Official full text](https://its.ntia.gov/publications/download/90-255_ocr.pdf). §§1.1–1.2 discuss restrictions and requirements; §2 develops scattering functions; §3 compares selected measured links. Original government research, not a universal field qualification.

**R4.** C. A. Nissen and P. A. Bello, *Measured channel parameters for the disturbed wide-bandwidth HF channel*, Radio Science, 2003. [Publisher article and DOI 10.1029/2002RS002746](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2002RS002746). Full text; measurement setup and channel-parameter analyses. The selected disturbed link and mode population limit geographic/statistical generalization.

**R5.** V. Dear et al., *Ionospheric Channel Impulse Response Measurement System for NVIS Propagation Mode Over Java Island Based on Low-Cost SDR Platform*, Radio Science, 2024. [Publisher full text and DOI 10.1029/2023RS007877](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2023RS007877). §§3–4; relative delay construction and measured CIR results. A geographically limited sounder study, with resolution and waveform structure that matter to interpretation.

**R6.** S. Fernandes, G. W. Perry, T. Trigo, N. A. Frissell, J. Gibbons and K. Collins, *Characterizing ionospheric variability through HF Doppler measurements: a statistical and numerical ray tracing analysis*, Frontiers in Astronomy and Space Sciences, 11 March 2026. [Full text, DOI 10.3389/fspas.2026.1713968](https://www.frontiersin.org/journals/astronomy-and-space-sciences/articles/10.3389/fspas.2026.1713968/full). §§2–3 and conclusions. Five-minute residual-frequency statistics; not a per-path complex-gain law.

**R7.** J. A. Aricoche, D. L. Hysell and Y. J. Morton, *Probing the main ionospheric trough with an Alaskan HF beacon network*, Frontiers in Astronomy and Space Sciences, 29 July 2026. [Full text, DOI 10.3389/fspas.2026.1848834](https://www.frontiersin.org/journals/astronomy-and-space-sciences/articles/10.3389/fspas.2026.1848834/full). §2.2 and §4 are decisive: Gaussian surface fitting is an estimator, not an assertion that the true spectrum is Gaussian; the stationarity discussion remains qualified.

**R8.** Q. Ji and H. Wu, *Adaptive Modulation Tracking for High-Precision Time-Delay Estimation in Multipath HF Channels*, Sensors 25(14), 4246, 8 July 2025. [Full text, DOI 10.3390/s25144246](https://www.mdpi.com/1424-8220/25/14/4246). §§3–4, tracking method, simulation and field experiment. Preserve its bandwidth and path-resolution limitations.

**R9.** Q. Ji and H. Wu, *High-Precision Time-of-Arrival Estimation in HF Sensor Networks via Multipath Separation and Independent Tracking*, Sensors 26(5), 1640, 5 March 2026. [Full text, DOI 10.3390/s26051640](https://www.mdpi.com/1424-8220/26/5/1640). §§2 and 4; MUSIC initialization, iterative path separation and independent tracking. The field SNR and sensing objective limit transfer to weak-signal decoding.

### Detection and state inference

**R10.** L. L. Scharf and B. Friedlander, *Matched Subspace Detectors*, IEEE Transactions on Signal Processing 42(8), 2146–2157, 1994. [Primary paper PDF](https://www.mssc.mu.edu/~daniel/mscs6960/spring2010/ScharfFriedlanderTSP94.pdf), [DOI 10.1109/78.301849](https://doi.org/10.1109/78.301849). Full text; §§II–V. Known subspaces and Gaussian assumptions are material to its invariant-optimality results.

**R11.** T. Zemen and C. F. Mecklenbräuker, *Time-Variant Channel Estimation Using Discrete Prolate Spheroidal Sequences*, IEEE Transactions on Signal Processing 53(9), 3597–3607, 2005. [Author full text](https://thomaszemen.org/papers/Zemen05-IEEETrSignalProcessing.pdf), [DOI 10.1109/TSP.2005.853104](https://doi.org/10.1109/TSP.2005.853104). §§III–V, especially §IV around pp. 3600–3601; effective dimension, truncation bias and variance.

**R12.** L. Wang and D. Lim, *Selection of the BEM Order in the Estimation of the Doubly-Selective Channel*, IEICE Transactions on Communications E91-B(6), 2037–2040, 2008. [Publisher record, DOI 10.1093/ietcom/e91-b.6.2037](https://www.jstage.jst.go.jp/article/transcom/E91.B/6/E91.B_6_2037/_article/-char/en). Publisher abstract inspected, full text unavailable. Used only for its stated order-selection problem and assumptions.

**R13.** K. J. Kim, M.-O. Pun and R. A. Iltis, *Joint Carrier Frequency Offset and Channel Estimation for Uplink MIMO-OFDMA Systems Using Parallel Schmidt Rao-Blackwellized Particle Filters*, IEEE Transactions on Communications 58(9), 2697–2708, 2010. [MERL full text](https://www.merl.com/publications/docs/TR2010-095.pdf), [DOI 10.1109/TCOMM.2010.080310.090053](https://doi.org/10.1109/TCOMM.2010.080310.090053). §IV, equations (19)–(23) and (32). Conditional Gaussian filtering does not make the overall particle/decomposition approximation exact.

**R14.** Z. Li, X. Xu, J. Wang, M. Si and Z. Wei, *A High-Precision Joint Synchronization and Channel Estimation Method for OFDM*, Telecom 7(2), 45, 16 April 2026. [Full text, DOI 10.3390/telecom7020045](https://www.mdpi.com/2673-4001/7/2/45). Equations (8)–(14), complexity analysis and conclusions. Analytical LS elimination leaves a nonlinear grid and differs from marginalizing a random gain.

**R15.** A. O. Kislal, M. Rajiv, G. Durisi, E. G. Ström and U. Mitra, *Is Synchronization a Bottleneck for Pilot-Assisted URLLC Links?*, IEEE Transactions on Wireless Communications 23(12), 17945–17958, 2024. [Institutional full text](https://research.chalmers.se/publication/543021/file/543021_Fulltext.pdf), [DOI 10.1109/TWC.2024.3458090](https://doi.org/10.1109/TWC.2024.3458090). Signal/timing model and numerical comparisons of delay dependence. Finite-blocklength, pilot-assisted setting.

### Multifrequency joint tracking

**R16.** J. Vilà-Valls, P. Closas and J. T. Curran, *Performance Analysis of Multi-frequency GNSS Carrier Tracking for Strong Ionospheric Scintillation Mitigation*, EUSIPCO 2017. [Conference full text](https://new.eurasip.org/Proceedings/Eusipco/Eusipco2017/papers/1570339391.pdf). §II-B explicitly assumes synchronized timing after acquisition; §§III–IV give joint filtering and synthetic evaluation.

**R17.** D. Wang, S. Zhang, T. Liu, Y. Chen and Q. Dong, *Performance analysis of cross-frequency Doppler-assisted carrier phase tracking*, GPS Solutions 27, 105, 12 April 2023. [Full text, DOI 10.1007/s10291-023-01434-4](https://link.springer.com/article/10.1007/s10291-023-01434-4). Equation (18), Property 3 and dynamic-stress discussion. Assisting-noise and bandwidth conditions are central.

**R18.** J. Sun, J. Wei, Z. Tang and Y. Duan, *Rapid High-Precision Ranging Technique for Multi-Frequency BDS Signals*, Remote Sensing 16(23), 4352, 21 November 2024. [Full text, DOI 10.3390/rs16234352](https://www.mdpi.com/2072-4292/16/23/4352). §§2–3, §4.2–4.3 and §5. The weak/selective-attenuation examples use five frequencies; no measured runtime or field validation is supplied.

### Delay Doppler representation and computational alternatives

**R19.** H. Qu, G. Liu, L. Zhang, M. A. Imran and S. Wen, *Low-Dimensional Subspace Estimation of Continuous-Doppler-Spread Channel in OTFS Systems*, IEEE Transactions on Communications 69(7), 4717–4731, 2021. [Institutional accepted manuscript](https://eprints.gla.ac.uk/238160/1/238160.pdf), [publication record](https://eprints.gla.ac.uk/238160/), DOI 10.1109/TCOMM.2021.3072744. Full accepted text inspected; transform basis and error analysis, including equations (53)–(60). Continuous spread and OTFS training assumptions matter.

**R20.** I. A. Khan and S. K. Mohammed, *A Low-Complexity OTFS Channel Estimation Method for Fractional Delay-Doppler Scenarios*, IEEE Wireless Communications Letters 12(9), 1484–1488, 2023, DOI 10.1109/LWC.2023.3274936. [Journal DOI](https://doi.org/10.1109/LWC.2023.3274936), [author-institution publication list](https://researchambit.iitd.ac.in/faculty/saifkm). The [inspected 2021 author manuscript](https://arxiv.org/html/2111.06009v1) is titled *Low Complexity Channel Estimation for OTFS Modulation with Fractional Delay and Doppler*. Technical discussion and §§III–V/appendix locators refer to that manuscript, not independently inspected final journal text.

**R21.** L. Liu, Z. Li, Q. Peng, Q. Luo, P. Xiao and H. Wu, *BEM-Assisted Low-Complexity Channel Estimation for AFDM Systems over Doubly Selective Channels*, IEEE Wireless Communications Letters, online 1 May 2026, DOI 10.1109/LWC.2026.3689566. [Inspected author manuscript v2](https://arxiv.org/html/2504.18901v2), [IEEE record](https://ieeexplore.ieee.org/abstract/document/11503297). GCE-BEM formulation and estimation sections. The 2025 preprint and 2026 journal item are one work, not independent confirmations.

**R22.** W. Benzine, A. Bemani, N. Ksairi and D. Slock, *Models, methods and waveforms for estimation and prediction of sparse time-varying channels*, IEEE Transactions on Wireless Communications, online 23 December 2025; paginated volume 25, 9623–9638, 2026, DOI 10.1109/TWC.2025.3644666. [Current author-institution record](https://www.eurecom.fr/en/publication/8552), [inspected arXiv v2](https://arxiv.org/abs/2504.13762v2). The manuscript title retains “Doubly Sparse”; §IV and Theorem 3 locators refer to that manuscript. Final typeset full text was not inspected.

**R23.** A. Fish and S. Gurevich, *Almost Linear Complexity Methods for Delay-Doppler Channel Estimation*, 2013 author notes with subsequent conference work. [Full manuscript](https://arxiv.org/html/1309.6036v1). §§I-E and II–IV. Arithmetic bounds require special sequences, sparse discrete shifts and stated genericity; carrier count alone supplies none of these.

**R24.** X. Li, E. Leitinger, A. Venus and F. Tufvesson, *Sequential Detection and Estimation of Multipath Channel Parameters Using Belief Propagation*, IEEE Transactions on Wireless Communications 21(10), 8385–8402, 2022. [Author manuscript](https://arxiv.org/abs/2109.05623), [DOI 10.1109/TWC.2022.3165856](https://doi.org/10.1109/TWC.2022.3165856). Full manuscript inspected; Bayesian model, association and numerical/measurement sections. Its input includes snapshot path estimates, not just an already-free path posterior.

**R25.** T. Bendory, N. Boumal, C. Ma, Z. Zhao and A. Singer, *Bispectrum Inversion With Application to Multireference Alignment*, IEEE Transactions on Signal Processing 66(4), 1037–1050, 2018; online 2017. [Primary full text in NSF repository](https://par.nsf.gov/servlets/purl/10059425), DOI 10.1109/TSP.2017.2775591. §II and high-noise sample-complexity discussion. Repeated shifted observations differ from a single modem bit waveform.

**R26.** L. Liu and F. Adachi, *Joint frequency-domain differential detection and equalization for DS-CDMA signal transmissions in a frequency-selective fading channel*, IEEE Journal on Selected Areas in Communications 24(3), 649–658, 2006. [Author full text](https://adachifumiyuki.jp/wp-content/pdf/publish_2006/06_pu_2006_liu.pdf), [institutional copy](https://tohoku.repo.nii.ac.jp/record/25391/files/ADACHI-Fumiyuki-01-08-db0094-01603718.pdf), DOI 10.1109/JSAC.2005.862416. Differential reference and error analysis; different modulation and block-fading assumptions.

**R27.** J. He and N. He, *Semi-blind OTFS channel estimation based on DPS-BEM*, Physical Communication 73, 102874, December 2025. [Publisher DOI 10.1016/j.phycom.2025.102874](https://doi.org/10.1016/j.phycom.2025.102874). Publisher abstract only. Retained as a lead combining a temporal basis, autoregressive coefficients and particle filtering; detailed algorithmic claims are not used.

**R28.** A. Mohebbi, W.-P. Zhu and M. O. Ahmad, *Two-Dimensional DPS-BEM Based Channel Estimation for MIMO OTFS Systems*, IEEE Transactions on Vehicular Technology 75(3), 4352–4368, March 2026; online 16 September 2025. [IEEE abstract and metadata](https://ieeexplore.ieee.org/document/11165134/), DOI 10.1109/TVT.2025.3610454. Publisher abstract only. No numerical or computational result is transferred to Robust.

**R29.** Xu et al., *High-Frequency Skywave Source Geolocation Using Deep Learning-Based TDOA Estimation and Bias-Regularized Semidefinite Programming with Field Evaluation*, Sensors 26(9), 2755, 29 April 2026. [Full text, DOI 10.3390/s26092755](https://www.mdpi.com/1424-8220/26/9/2755). §§2.1–2.3 and §5.4. Regional ionospheric background plus synthetic correlated parameter variation; field geolocation is not event-matched validation of every simulated CIR statistic.

### Relevant items whose inaccessible primary text limits this review

The following are discovery leads, not evidence for the report's technical conclusions:

- Niu et al., *Channel Measurement and Statistical Modeling for Wideband Ionospheric Scattering Communication in the High-Frequency Band*, reported as an October 2026 IEEE Transactions on Antennas and Propagation article. [Publisher entry](https://ieeexplore.ieee.org/document/11578181/), DOI 10.1109/TAP.2026.3705045. The accessible discovery results were secondary bibliographic/abstract reproductions; the primary text could not be checked.
- Niu et al., *Wideband HF Ionospheric Channel Evolution Over Distances: Measurement and Statistical Modeling*, 2026. [Publisher DOI 10.1109/LAWP.2026.3696384](https://doi.org/10.1109/LAWP.2026.3696384). Only secondary discovery material was accessible. This and the preceding item should not automatically be counted as independent campaigns.
- Yang, Zhan and Huang, *Robust GNSS Triple-Carrier Joint Estimations Under Strong Ionosphere Scintillation*, 2020. [Publisher DOI 10.1007/978-981-15-3707-3_53](https://doi.org/10.1007/978-981-15-3707-3_53). Publisher full text unavailable. No numerical claims from third-party reproductions are used.
- Yang, Huang and Zhan, *Hybrid Carrier Tracking with Decoupled Local Filters in Multi-Frequency GNSS Receivers*, 2024. [Publisher DOI 10.33012/navi.672](https://doi.org/10.33012/navi.672). Full text unavailable. No operation count from a newsletter is treated as a verified paper result.

These access limits leave potentially useful recent details unresolved. They do not justify claiming that no better model or receiver exists. The inspected primary literature already supports the main distinctions and the portfolio of candidates.

## Source fingerprints

SHA-256 fingerprints of the selected implementation files, taken during this review. These identify the inspected source, not a compiled binary or a release certification.

| Source file | SHA-256 |
| --- | --- |
| [src/robust_profile.cpp](/home/user/___quick/p/_cur/dataPump/pumpModem/src/robust_profile.cpp) | 3253421df71c0f5847a13c943eac9e4b5fe29e6ec91edc22df4696768df08132 |
| [src/triad_modem_v3.cpp](/home/user/___quick/p/_cur/dataPump/pumpModem/src/triad_modem_v3.cpp) | 8fc25e14c12bf344948b98330532301c1587b13d1c045986e82f2d5dab00455b |
| [src/triad_receiver_fine.cpp](/home/user/___quick/p/_cur/dataPump/pumpModem/src/triad_receiver_fine.cpp) | ec24034eb42e6b0f8caec237a032e608a0f4402d64786b4fa289c31663ca5214 |
| [src/triad_receiver_coarse.cpp](/home/user/___quick/p/_cur/dataPump/pumpModem/src/triad_receiver_coarse.cpp) | 30fcffe5864ae77a4a387dbe780ee36163d6f686a665e03f466711c1142083d5 |
| [src/triad_gaussian_kernel.cpp](/home/user/___quick/p/_cur/dataPump/pumpModem/src/triad_gaussian_kernel.cpp) | ace1942780e6e3875cb8c03113bbf014dd6f49ad86d7871ebb572de272799dd0 |
| [src/live.cpp](/home/user/___quick/p/_cur/dataPump/pumpModem/src/live.cpp) | f7640f7aa30f6570830ba13d2660688b8e855185269db5697e1b4b970bd6e067 |
| [src/channel_spread.hpp](/home/user/___quick/p/_cur/dataPump/pumpModem/src/channel_spread.hpp) | 8c1e14c3db641199bc706cc93cc40246330f3bc9da41a0f698f40d2e115c1955 |
| [src/channel_perturbation.hpp](/home/user/___quick/p/_cur/dataPump/pumpModem/src/channel_perturbation.hpp) | 2484b9b04a89b907a0f2301be9a2056da73d086d5450f15a3cc72093edbe2bc4 |
| [src/triad_simulation_stream.cpp](/home/user/___quick/p/_cur/dataPump/pumpModem/src/triad_simulation_stream.cpp) | eaff633c9ca09628f2f614bce569d965e462b40bbafcbbb0d04fae62c18c5195 |
| [include/datapump/triad_modem_v3.hpp](/home/user/___quick/p/_cur/dataPump/pumpModem/include/datapump/triad_modem_v3.hpp) | 34591b96d30596128c9780eb5dfab349f9981de1f8525b02572969560763de4f |
| [src/gui/controller.cpp](/home/user/___quick/p/_cur/dataPump/pumpModem/src/gui/controller.cpp) | b16d426bbba2ff70d1830320f1b2ca83302bb5e937b003194c7bff0532b52da5 |
| [src/gui/screen_console.cpp](/home/user/___quick/p/_cur/dataPump/pumpModem/src/gui/screen_console.cpp) | d7c3ff1610b5bf8c37ca3aa3d65e472d73f0073ee73b9e0af15d8364922dcbf7 |
| [src/tuning_presets.cpp](/home/user/___quick/p/_cur/dataPump/pumpModem/src/tuning_presets.cpp) | 31e1ac552bea10b24036f959485241532066e6aa343931c04a46a65e8734b293 |
| [src/triad_receiver.cpp](/home/user/___quick/p/_cur/dataPump/pumpModem/src/triad_receiver.cpp) | c48c457ab6a9bccd0ca564b614dcb8e0248796bff6b0030c8373af2d960656b5 |
| [src/triad_receiver_basis.cpp](/home/user/___quick/p/_cur/dataPump/pumpModem/src/triad_receiver_basis.cpp) | 63012004d1b2a446faf10b76ceabfd72885b7e06fa9851fb8a57605d5e1e6798 |
| [include/datapump/modem.hpp](/home/user/___quick/p/_cur/dataPump/pumpModem/include/datapump/modem.hpp) | 00db466b2ae68f6cca798ff231168fc1b99b18d1792ba8abfbb1db92df47e789 |
| [include/datapump/triad_receiver.hpp](/home/user/___quick/p/_cur/dataPump/pumpModem/include/datapump/triad_receiver.hpp) | 715026bf4285528053a7ddc23cb0767ba84561191c1fcbd81eb751abb99933d8 |

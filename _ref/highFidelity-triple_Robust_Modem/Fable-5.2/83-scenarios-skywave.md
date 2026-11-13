# Source-bound HF skywave scenarios

- **Part of.** [High-fidelity weak-signal channels](README.md); source evidence and estimators are in [30](30-skywave-signal-level-and-outages.md), [31](31-skywave-dispersion-midlatitude-nvis-longhaul.md), [32](32-skywave-dispersion-highlatitude-equatorial.md), and [33](33-hf-channel-models-and-simulators.md). Numerical realization is governed by [60](60-simulation-methods-and-validation.md).
- **Status, 13 November 2026.** Mathematical research specification, not an implemented simulator or modem result. Standard controls, realizations matched to measured summaries, explicit hypotheses and empirical replays are distinct. Every number labelled **H** below is an experimental design assumption, not a measured natural distribution. The empirical branches remain open where their required data are unavailable.

## 1. Reference plane and one physical operator

Let `x(t)` be the complete analytic transmitted waveform, including Helper and every DATA carrier, relative to RF reference `f_c`. All actual carrier offsets and public timing come from the frozen canonical native fixture in [00 §7](00-method-and-modem-context.md#7-reproducible-source-and-evidence-contract). Default numerical UTC epoch is 2026-01-01T00:00:00Z and `f_c=10 MHz`, except explicit source-frequency overrides below; this date is a design label, not a reconstructed historical ionosphere. Apply

`y_s(t)=A(t) Σ_p b_p(t) exp{jθ_p+j2π∫₀ᵗν_p(u)du−j2πf_c[τ_p(t)−τ_p(0)]} x(t−τ_p(t))`.

Here `p` indexes physical delay components, `b_p` includes its voltage weight, fading and availability envelope, and `θ_p` includes the fixed RF propagation phase at time zero. At offset `f`, the same operator contains `exp(−j2πfτ_p(t))`. Thus all carriers inherit one impulse response, not independent per-carrier channel draws. Add receiver noise after this operator at the receiver antenna-terminal reference plane, then apply the declared radio/ADC model. Noise density does not fade with the signal.

`ν_p` is **residual** centroid shift. The moving-delay contribution at RF frequency `f_c+f` is already `−(f_c+f)τ′_p(t)`; do not add it a second time. Delays are fixed except in the explicitly moving-delay control below. Phase, fading, delay-line state, oscillator and noise advance across processing chunks, outages, quiet slots and message boundaries. There is no per-slot phase reset or per-realization power normalization.

**H level grid.** Set mathematical reference power `C_ref=1 W` and independently test `C_ref/N0 ∈ {0,8,20,32} dB-Hz`, with `N0=10^(−c/10) W/Hz`, flat proper-complex Gaussian receiver noise. These are deterministic DATA-reference sensitivity points, not per-carrier requirements or predicted HF received watts. Use the native reference in 00: `mu=report.data_reference.mean_norm`, `G=report.data_gain`, `R_emit=mu*G²`, then scale the whole emitted composite once by `sqrt(C_ref/R_emit)`. Keep DATA reference power fixed when adding Helper and report its extra power separately. Do not redefine power using the realized message, its gaps or faded samples. At analytic rate `F_s`, full-Nyquist complex noise variance is `N0F_s`; real-audio conversion and receiver-filter integrals follow [60](60-simulation-methods-and-validation.md). A physical link-budget run instead obtains compatible terminal signal/noise levels from [30](30-skywave-signal-level-and-outages.md) and [50](50-noise-and-interference-mf-hf.md), without normalizing from faded samples. Label these two run classes separately.

## 2. Exact probability laws, initialization and normalization

**G, Gaussian-spectrum control.** Each independent tap has a proper complex stationary Gaussian process `z`, mean zero, `E|z|²=1`, zero pseudo-covariance, and `R_G(u)=exp(−π²D²u²/2)`. `D=2σ` is the full two-standard-deviation width of the Doppler **power** spectrum, prior to adding its centroid. Initial state has the stationary joint law, including correlated prehistory; it is not zero or one. Set `θ_p=0` for diffuse Gaussian taps, since a fixed phase rotation preserves their distribution. This covariance specifies every finite-dimensional distribution. Small exact covariance factorizations in [60](60-simulation-methods-and-validation.md) are numerical oracles; a production finite-filter approximation must meet that chapter's declared covariance/spectrum/interpolation checks. No audio-length dense factorization is required.

**L, Lorentzian disagreement control.** Match the G comparator's `1/e` time `T_c=√2/(πD)`, and use `R_L(u)=exp(−|u|/T_c)`. On a uniform grid `Δ`, initialize `z₀~CN(0,1)` and use `z_{n+1}=a z_n+√(1−a²)ε_n`, `a=e^(−Δ/T_c)`, independent `ε_n~CN(0,1)`. This is the exact stationary sampled OU law. Continuous PSD is `2T_c/[1+(2πνT_c)²]`; it has **no finite second Doppler moment**, so `D` labels the matched Gaussian, never a Lorentzian `2σ`. Interpolation/bandlimiting changes the continuous law and is validated separately under [60](60-simulation-methods-and-validation.md). L is a hypothesis, not a fitted natural channel.

**C, coherent-fraction hypothesis.** Replace unit diffuse gain by `√[K/(K+1)]exp(jθ)+√[1/(K+1)]z(t)`, `K∈{0,1,10}` in **linear power ratio**. `K=0` is Rayleigh; the other cases are 0-dB/10-dB Rician hypotheses. Draw `θ` uniformly on `[0,2π)` initially, independently per coherent component, and apply the common continuous centroid/delay phase. These factors are not inferred from Watterson or HiCIRF. `E|gain|²=1`; no phase reset occurs during outage or recovery.

**Power.** A source power `P_dB` gives `q=10^(P_dB/10)`; a source RMS gain `r` gives `q=r²`. Independent components use `b_p=√(q_p/Σq)z_p`. Keep original component values when rounded aggregate tables differ. Identical-delay O/X powers add only for independent proper gains. Record one ensemble normalizer, not a sample-by-sample or finite-record energy normalizer.

**Finite rendering.** Use the explicit engine, uniform/normal transform and paired roots in [00 §7](00-method-and-modem-context.md#7-reproducible-source-and-evidence-contract), retaining domain 6 for receiver noise. For physical component `p=0,1,…`, stochastic subdomains are `1000+32p+k`: `k=0` independent diffuse gain, `1` shared-mode gain, `2` coherent phase, `3` slow gain, `4` availability. Shared variables are generated once and reused, never independently reseeded. Record algorithm/version, seed, sample and gain grids, filter support, delay interpolator, precision and actual covariance. Stationary G/L laws have infinite time support; a finite numerical approximation may not wrap periodically inside an episode. Pre-emission input is zero. Maximum physical delay is the largest tabulated delay plus any within-mode width and motion; process the entire physical and numerical filter tail. Numerical warm-up and truncation are implementation quantities, not invented physical time limits. The frozen construction and acceptance errors in [60](60-simulation-methods-and-validation.md) govern realizations; this mathematical specification alone does not pass those checks.

## 3. Stationary controls for every stratum

All profiles have `A=1`, fixed delays, continuously present components, zero birth/death rate, and G fading unless explicitly varied. Long stationary runs beyond the measured record duration are labelled extrapolations.

### F.1487 comparator family

Every row is exactly two independent equal-power G taps at delays `(0,δ)`, common `D`, zero centroids, no coherent component. Values are from **ITU-R F.1487 Annex 3**, checked in [33](33-hf-channel-models-and-simulators.md).

| Profile / stratum | `δ` ms | `D` Hz |
| --- | ---: | ---: |
| F-MQ / S0 midlatitude quiet | 0.5 | 0.1 |
| F-MM midlatitude moderate | 1 | 0.5 |
| F-MD midlatitude disturbed | 2 | 1 |
| F-N / S2 NVIS disturbed | 7 | 1 |
| F-HQ high latitude quiet | 1 | 0.5 |
| F-HM high latitude moderate | 3 | 10 |
| F-HD / S4 high latitude disturbed | 7 | 30 |
| F-LQ low latitude quiet | 0.5 | 0.5 |
| F-LM low latitude moderate | 2 | 1.5 |
| F-LD / S6 low latitude disturbed | 6 | 10 |

**DRM-6 tropical NVIS** is a distinct standard comparator: independent G gains, delays `(0,2,4,6) ms`, RMS gains `(.5,1,.25,.0625)`, centroids `(0,1.2,2.4,3.6) Hz`, widths `(.1,2.4,4.8,7.2) Hz` (ETSI ES 201 980 Annex B / BS.1615-3 Table 14). Divide voltage gains by `√1.31640625`. These standard labels imply neither occurrence probabilities nor worst-possible natural channels.

### S1, Watterson I1/I2/I3 summary-matched realizations

Each tuple is `(delay µs, component power dB, centroid Hz, D Hz)`. Equal delays identify independent magneto-ionic components; no aggregate tap is added. Source: Watterson 1969 Tables 4/6/8, with transcription and aggregation discrepancies checked in [31](31-skywave-dispersion-midlatitude-nvis-longhaul.md).

| Case | Literal component tuples |
| --- | --- |
| I1 | `(40,−4.1,.0022,.0073); (40,−4.3,.0170,.032); (290,−7.2,.0089,.144); (1139,−13.5,−.167,.340)` |
| I2-L | `(40,−4.1,−.0008,.0064); (40,−5.5,.0127,.0084); (290,−5.9,.0159,.180); (590,−17.6,.108,.334); (1126,−12.6,.118,.336)` |
| I2-S | Interchange only the first two powers in I2-L; this is a declared hypothesis resolving the source aggregate-centroid discrepancy. |
| I3 | `(445,−3.8,.076,.036); (445,−5.7,.134,.032); (750,−10.8,.121,.010); (750,−10.6,.141,.013); (1088,−12.9,.121,.015); (1088,−10.4,.151,.021)` |

Use `f_c=9.259 MHz` for I1/I2, `5.864 MHz` for I3. Source observation durations are `780/600/780 s`; relative delays do not establish absolute propagation time. These are synthetic realizations from fitted summaries, **not raw-channel replay**. If a complete modem message exceeds the source duration, retain its full physical completion and label the run a stationary-extension hypothesis rather than truncating the message.

### Source envelopes and controls must remain separate

| Stratum / observed input | Defined comparator or hypothesis | Empirical limitation |
| --- | --- | --- |
| S1, UK trough paths 1,440/1,800 km: 2011 winter-night 95th-percentile detected-mode spans 2.2–3.6 ms at selected 4.6–11.1 MHz, excluding modes >12 dB below strongest | I1–I3 plus F-MM/F-HM; no random sampling from a guessed percentile distribution | First-to-last detected-mode span is not RMS tap delay; ordered joint mode powers/Doppler records are needed for a fitted generator. |
| S2, Male 2021, 5.4 MHz, 97 km, twelve days: O/X availability .576/.722, union .860 during 07–16 UTC; conditional mean spreads .33/.31 ms and maxima 2.71/2.89 ms | F-N and S2-H below | Detector/active-frame conditioning; percentages do not identify transition rates, O/X delays or cross-frequency covariance. Happy-hour O/X correlation below .7 with nearly 40% probability is one-frequency evidence only. |
| S3, Texas 8.77-km campaign, 5.3/7.2 MHz: echo clusters near 1.9/3.8 ms | **S3-H:** three independent G taps at `(0,1.9,3.8) ms`, powers `(1,1,1)/3`, `D=.1 Hz`, zero centroids, `f_c=5.3 MHz` | Delays are observation-inspired; simultaneous equal powers, width, centroid and frequency pairing are hypotheses, not an extracted joint capture. |
| S4, DAMSON/NRL high latitude: instrument- and threshold-conditioned widths, including DAMSON central 80% composite estimator, ±40-Hz/12.5-ms window | F-HD; diffuse and L alternatives below | Composite between-mode width is not per-tap `2σ`. Extreme short-path widths approach instrument limits. Raw ordered scattering functions and masks are still needed. |
| S6, Kwajalein spread-F: threshold widths >6 Hz at−10 dB, about 15 Hz at−30 dB, roughly 1-ms delay structure | F-LD, DRM-6; **S6-H:** one 17-microtap mode of width 1 ms, G `D=3 Hz`, zero centroid, `f_c=10 MHz`, both `ρ=0/1` below | The selected `D`, carrier and exponential delay profile are hypotheses, not a fit to threshold widths or a transferable equatorial climatology. |
| S5, May 2024 event: Ponomarchuk 2025 ionograms/MOF/mode-presence time series on 1,372/3,034/3,901-km paths, oblique cadence 5min and vertical/NVIS usually≤1min | S7-H supplies a defined outage comparator | **Empirical event trajectory remains open:** requires actual digitized timestamps, values, axis uncertainty and missing-data mask. Figures can support mode/MOF replay, not coherent response, Doppler spread or calibrated modem C/N0. |

Source details and detection conventions remain in [31](31-skywave-dispersion-midlatitude-nvis-longhaul.md) and [32](32-skywave-dispersion-highlatitude-equatorial.md). Marginal extremes from separate records are never combined and described as an observed joint event.

## 4. Independent and shared dependence alternatives

Independent physical taps imply `E[H(f,t)H*(g,s)]=Σ_j p_j R_j(t−s)exp[j2πν_j(t−s)]exp[−j2π(f−g)τ_j]`. Thus all actual carriers, including Helper pilots, have physically consistent frequency covariance. Two equal taps have same-time normalized magnitude `|cos(π(f−g)δ)|`. Independent per-carrier fading is not a substitute for this operator.

**H diffuse mode.** Use 17 physical microtaps `u_j=jw/16`, `j=0…16`, weights `p_j=exp(−4j/16)/Σ_k exp(−4k/16)`, hard width `w`. All share the mode's G width/centroid (or separately its L alternative). Set `z_j=√ρ z_c+√(1−ρ)ε_j`, with independent unit processes `z_c,ε_j`. Evaluate `ρ=0` and `ρ=1` separately. Gains are `√p_j z_j/√Cρ`, `Cρ=(1−ρ)+ρ(Σ√p_j)²`, which fixes ensemble response power at zero offset to one. Expected power elsewhere may differ and is reported, never equalized per carrier. This is a finite exponential **assumption**, not a measured HF delay law. It supplies valid shared versus independent latent alternatives inside one causal impulse response; neither is a rigorous decoder-performance bound.

**H slow gain.** Let real stationary OU `L(t)` have mean 0, standard deviation `s=4 dB`, correlation time 60 s; exact grid step `L_{n+1}=aL_n+s√(1−a²)η_n`, `a=e^(−Δ/60)`, `η~N(0,1)`, initial `L₀~N(0,16)`. Set `A=exp(κL/2−κ²s²/4)`, `κ=ln10/10`; then `E[A²]=1`. Compare one common process for the entire path to independent processes per physical mode. The 4-dB/60-s pair is an explicit sensitivity law, not a validated climatology or the unpublished Harris model. No per-frequency replacement is permitted.

O/X correlations cannot be copied into Helper/DATA covariance. Polarimetric diversity needs a declared Jones matrix and antenna projection; this scalar receiver suite does not qualify an unspecified dual-polarization system.

## 5. Nonstationarity, birth/death, outage and recovery

Let public physical-slot duration be `T`, and `r(v)=0` for `v≤0`, `(1−cosπv)/2` for `0<v<1`, `1` for `v≥1`. All rates, schedules, ramps and unreferenced powers here are **H**, with continuous voltage envelopes and continuing latent state.

**S2-H availability law.** From the reported marginals/union derive weights `(both,O only,X only,neither)=(.438,.138,.284,.140)`, assuming a common denominator. At every 60-s boundary independently draw one state, including an initial state at −60 s. During the first 5 s of each new block, interpolate each binary **voltage gate** from its preceding value to its new value with `r`; then hold. Use O/X-labelled G taps with delays `(0,.33) ms`, powers `(.5,.5)`, `D=.1 Hz`, zero centroids, `f_c=5.4 MHz`. The delay is **not** asserted to be measured O/X separation; all tap assignments and time constants are hypotheses. Compare independent fades to one shared fade with the zero-offset covariance normalizer from §4. Latent processes exist and advance while gated off. The categorical weights describe drawn block states, not threshold availability after 5-s ramps; no empirical transition rate is claimed.

**S4-H / S6-H mode transition.** Start from F-HM/F-LM. At `3T`, raise an independent diffuse mode of width 1 ms, G `D=10 Hz`, zero centroid, using gate `r((t−3T)/(.25T))`; lower the original modes by its complement. Reverse at `6T` over`.25T`. New latents exist from episode start, so birth/death does not reset phase. Independent and shared microtap variants are separate. Crossfade reference power is `(1−r)²+r²`, including its deliberate maximum 3-dB dip; do not normalize it away. This is neither a fitted storm nor a postsunset occurrence law.

**Moving-delay check.** On S3-H add `100 µs+100 µs·sin(2πt/120s)` to all base delays; residual centroids remain zero. Delays stay nonnegative. Derivative is `(2π·100 µs/120s)cos(2πt/120s)`; at 5.3 MHz RF Doppler amplitude is 27.7507 Hz with the opposite sign. Actual carriers retain their own absolute-frequency scaling. Period, excursion and joint motion are hypotheses for checking phase continuity and Doppler accounting, not measured TID parameters.

**S7-H absorption/outage.** Define `g(t)=1−r((t−3T)/(.25T))+r((t−6T)/(.25T))`. It is one before`3T`, zero from`3.25T` to`6T`, and one after`6.25T`. Multiply the entire selected S0/S4/S6 operator by (a) `g(t)` for synthetic exact outage, (b)`10^[−40(1−g(t))/20]` for 40-dB residual signal, or (c)the same expression with 100 dB as a separately labelled extreme-loss hypothesis. Sounder no-return never establishes case(a). Noise and latent state persist; recovery reveals the continuing realization, not a reset channel. Do not combine independent absorption maxima into a purported natural joint worst case.

Stationary I1–I3 have transition rate zero. A long-message variant with §4 OU gain or S7-H is labelled `I*-H`, not a 1967 replay. All synthetic transition rules are specified: zero for stationary controls, independent 60-s categorical blocks for S2-H, deterministic ramps for S4/S6/S7. Empirical transition rates remain unknown.

## 6. Critical input checks and remaining empirical gates

| Input | Source or assumption | Required check / allowed claim |
| --- | --- | --- |
| F profiles | F.1487 Annex 3, [33](33-hf-channel-models-and-simulators.md) | Equal independent power, Gaussian PSD, `2σ` versus offset distinction; passing qualifies the named comparator. |
| I1–I3 tuples | Primary summary tables, [31](31-skywave-dispersion-midlatitude-nvis-longhaul.md) | Linear power sum, weighted centroid, original discrepancy retained in I2-L/S; source-duration applicability. No raw replay. |
| DRM-6 | ETSI/BS.1615-3, [33](33-hf-channel-models-and-simulators.md) | Square RMS gains; preserve separate widths/centroids. |
| S2/S3/S6 observations | Different conditional estimators, [31](31-skywave-dispersion-midlatitude-nvis-longhaul.md)/[32](32-skywave-dispersion-highlatitude-equatorial.md) | Keep source path/date/frequency/denominator/resolution; added law remains H. |
| G/L/C, microtaps, OU, all gates | Mathematical/design assumptions | PSD covariance, normalization, phase derivative, no resets, common composite operator, finite numerical convergence under [60](60-simulation-methods-and-validation.md). |
| S5 processed event | Ponomarchuk 2025, [30](30-skywave-signal-level-and-outages.md) | Still requires digitized trajectory and error/missingness mask. No coherent/Doppler/C/N0 information may be invented from MOF. |
| WHISPER / QAN-ALE / ELOISE replay | Distinct potential datasets | WHISPER thesis describes complex sounding but no raw data package is obtained. QAN-ALE version 1 and its CC0 archive are now inspected in 32: 5,588 MATLAB files, with six bounded schema samples showing processed receive-burst observables rather than raw I/Q. Calibration/unit dictionary, corpus-wide consistency and attempted-transmission denominator still need qualification; archive access is no longer the blocker. ELOISE access is case-by-case by its owner. These empirical branches remain open. |
| Flare/PCA loss scaling | Source-specific events/models, [30](30-skywave-signal-level-and-outages.md) | Preserve path illumination/frequency range. Recent Ottawa–Alert observations give conditional scheduled-pulse loss statistics using spatially displaced riometers; they do not establish a universal riometer-to-HF outage conversion. |

Scalar arithmetic checks for this specification: F-MQ `T_c=4.50158s`; F-HD `.0150053s`; DRM-6 power sum`1.31640625`; S2 state weights sum to one and reproduce all three input fractions; moving-delay amplitude`27.7507Hz`; two-tap correlations at 75-Hz separation`.993068` for`.5ms` and`.0784591` for`7ms`. These are mathematical controls, not measured channel agreement. Numerical synthesis, receiver outcomes, field calibration and communication probability have not been tested here.

## 7. Public knowledge, continuous messages and scoring

Use [00 §7](00-method-and-modem-context.md#7-reproducible-source-and-evidence-contract)'s canonical HF fixture: rate 36, added symbols 10, FHSS off, requested 16-s bit duration, `Crypto::amateur()`, uppercase ASCII `A`, compression true, FEC off, content limit 16 bytes. Its thirteen-bit source cross-check is `1111101000001`; native execution has not yet confirmed this fixture. Keep the actual returned source/slot geometry and full public receiver hypotheses. The translated Helper-off baseline uses 960 samples/s only when its complete source/channel containment guard passes; otherwise use 48,000 samples/s. Source, channel output and receiver public profile must use exactly the same final sample rate; an internal oversampled renderer must explicitly low-pass and return that rate, with no implicit profile resampling. The requested 16-s HF setting can return 23.2 s of active geometry; that is not permission to replace the returned slot recurrence, halo or quiet aperture with either 16 s or the historical 11.6-s row. The separate full Helper/DATA comparison uses the 48,000-sample/s physical-band configuration and actual returned Helper placement/powers from 00. Never discard Helper or a large Doppler tail to fit the cheap baseline.

For the defined H controls freeze public configuration before drawing the channel:
`PublicProfileConfig.cfo_bound_hz=100`, `arrival_low_seconds=0`,
`arrival_high_seconds=.05`, `spread_delay_ms=30`,
`delay_excursion_seconds=.0002`, `clock_bound_ppm=0`, `audio_pair_ppm=0`.
Use matching receiver `frequency_bound_hz=100`, `spread_delay_seconds=.03`,
`delay_excursion_seconds=.0002`, zero relative/endpoint clock bounds,
`time_varying_channel=true`, `time_varying_common_doppler_hz=100` and
`time_varying_atmospheric_hz=0`, automatic native coarse precision and the
canonical public hypothesis/admission limits. These are finite public search
choices, not claims that all Gaussian/Lorentzian spectral tails lie inside a
hard ±100 Hz support or that the native hypothesis family exactly fits H.
Keep out-of-search failures in the declared outcome denominator. For the
finite numerical channel, require its 60-defined bandwidth/covariance gates;
use the 48 ksample/s fixture whenever 960-sample containment cannot be proved.
The C/N0 reference supplies public `data_basis_gain` and `noise_variance` via
00. Set receiver/filter tail buffers from the selected finite delay/filter
support before drawing a realization; missing a containment or memory gate
is setup failure, never silent physics truncation.

Ground-truth taps, seeds, channel state, payload length, emitted end time, scenario identifier, noise-only labels and outage boundaries are evaluator-only. An oracle channel estimate or source MOF may narrow acquisition only in a separately labelled oracle experiment. Source slots start at 0 and run through`M−1`, `M=wire_bits()`; final quiet slots are`M,M+1`,13/14 for the stated fixture. Keep the global ordinal and use the common 00/86 horizon: q0 is the first slot with an entirely uncontaminated scoring-input window after source end plus propagation/filter support, and processing continues through q0+2 including lookahead. This adds an observation margin without changing the two-quiet-slot completion rule. The emitted endpoint never selects received geometry or supplies a completion shortcut.

Scheduled transitions at 3T/6T occur inside this message, with `T` the actual returned uniform public-slot recurrence. Require `M≥8` before admitting that fixture; a changed codec failing that guard is a configuration failure, not permission to shorten the event or hand a new length to the receiver. An additional exact interior-null counterexample uses 00's slots`floor(M/2)` and its successor, with source/filter halos from actual quiet-score apertures, because an arbitrary time-domain fade need not suppress every contributing sample. The smooth S7-H transition remains a separate physical-gain example. Source-limited intervals shorter than the complete message imply an explicitly labelled stationary extension, never a truncated completion test.

Match waveform/noise seeds and receiver settings between baseline and adverse controls. Record channel truth separately (per-carrier C/N0, covariance, source-matched estimator, censoring and mode support). Score Helper acquisition including unsuccessful candidates, DATA tracking, bit/null/erasure decisions, reacquisition, full message correctness and two-quiet-slot completion. A long interior absorption fade may itself trigger two quiet decisions; count premature completion against transmitter truth, without telling the receiver an outage occurred. Resumed transmission does not retroactively make that premature completion successful. Transport EOF and local time limits are incomplete/censored outcomes.

Apply bounded statistics from [86](86-statistics-and-calculation-checks.md), not exhaustive parameter products. One realization checks mechanics, not rare-event reliability. Empirical joint carrier covariance, unknown epoch/ordinal acquisition, population outage probabilities and full complex HF replay remain unqualified. Better models and new data may supersede any H branch while retaining its matched comparator and conditional conclusions.

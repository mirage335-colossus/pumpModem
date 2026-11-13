# Method, conventions and modem context

- **Purpose.** Shared foundation for the chapters in this directory: how evidence is graded, which conventions are used, and what the literature is being compared with — the triple Robust Modem's waveform and its present simulated channel.
- **Scope.** Documentation only. No repository file outside this directory was changed, and nothing was built or run on the modem. Numbers marked [E] are this record's own estimates; the larger ones are collected in [90](90-cross-check-calculations.md).
- **Source revision.** Original inspection used `0801e5c7`; the completion pass starts from clean `33f6e461fce12f4079870091b6d1aaa9c2f50821`. A Git comparison found no changes in production source, headers, tests, build definitions or maintained guides between the intervening research revision `07bcbc5d` and this baseline. Source-derived contracts were rechecked; the historical planner receipts were not rerun. Section 7 separates exact source geometry from their rounded observation summaries.
- **Lifecycle.** Remove this directory once its applicable findings have been distilled into the maintained guides ([channel models](../../../docs/channel-models.md), [Robust operating guide](../../../docs/indexes/robust.md)) and into scenario records of the appraisal suite under `_appraisal/highFidelity-triple_Robust_Modem/`. No maintained guide could be edited during this work, so this purpose and trigger are recorded only here.

## 1. The question

The modem's existing simulations simplify on purpose: some settings are stresses beyond any known condition, others mirror the manual test list. That finds practical faults, but it does not say how the modem would behave in the conditions that nature and real equipment actually produce. This directory collects what the literature supports about five families of real weak-signal conditions, from moderate to worst case:

1. below 9 kHz with a 200 ft (61 m) antenna;
2. groundwave near 1 MHz;
3. groundwave near 30 MHz;
4. skywave, 1–30 MHz;
5. Earth–Moon–Earth, with emphasis on 5.8 GHz.

For each it asks three things. What is the real channel, in the terms a very narrowband, very long-integration modem cares about? How does that differ from the present presets? And what simulation implementations could reproduce it, with what limits?

It does not answer whether the modem works in those conditions. That needs experiments that have not been built.

## 2. Evidence grades and access status

The chapters use the following source-type labels. A label alone does not establish peer review, access to full text, or applicability; those are separate attributes.

| Grade | Meaning |
| --- | --- |
| **[S]** | Standard or recommendation (ITU-R, MIL-STD, STANAG, ETSI). |
| **[P]** | Research paper or preprint. Publication and peer-review status must be checked separately; entries explicitly marked preprint are not thereby established as peer reviewed. |
| **[T]** | Government or laboratory technical report. |
| **[B]** | Textbook or monograph. |
| **[G]** | Grey literature: amateur-radio articles, conference slides, websites, mailing lists, software documentation, manufacturer data. |
| **[H]** | In-house record of this project. |
| **[E]** | This record's own estimate or derivation. The formula or method is given. |

Each reference also carries an access status. **Read** means the supporting passage was read in the full text or an author's or repository copy, usually through text extraction rather than by eye. **Abstract** means only an abstract, index record or search snippet was seen. **Secondary** means the value is quoted through another source, which is named. A number that could only be recalled and not located is marked as unverified, or left out.

A grade says where a number comes from. It does not say the number applies to another frequency, path, season or estimator. Each chapter states those conditions next to the value.

## 3. Conventions

These differences in convention are large enough to change conclusions, so they are fixed here.

### 3.1 Level, loss and noise

**In the modem GUI**, "Path loss" is one number subtracted from "Transmit Power" to give the signal power at the receiver, and the simulated noise density defaults to −164 dBm/Hz (`src/gui/link_planner_model.hpp:17`). The simulated link is then

    C/N0 [dB-Hz] = P_tx [dBm] − PathLoss [dB] + 164.

**In the literature** four different quantities are in use.

- *Basic transmission loss* L_b is defined between isotropic antennas. For a ground-wave field strength E quoted for the ITU-R reference radiator (1 kW e.m.r.p., a short vertical monopole on perfectly conducting ground, 300 mV/m at 1 km), L_b = 142.0 + 20 log f_MHz − E[dB(µV/m)] [S, ITU-R P.368].
- *System loss* adds the antenna and matching inefficiencies. Below about 2 MHz, and above all below 9 kHz, the transmitting antenna's inefficiency is tens of decibels and belongs to the link.
- *Radar loss* for Earth–Moon–Earth follows the radar equation with the Moon's cross-section.
- *External noise* is given as the factor F_a in decibels above kT0 at the terminals of a lossless reference antenna, so that N0 = −174 dBm/Hz + F_a [S, ITU-R P.372]. The −164 dBm/Hz default corresponds to F_a = 10 dB.

**Why "path loss against −164 dBm/Hz" misleads below 30 MHz.** When external noise dominates and the receiving antenna applies the same effective attenuation to the wanted field and the external-noise reference, that common attenuation cancels in C/N0. This requires compatible reference planes, polarization and directional response. A changed antenna pattern or polarization, spatially distributed noise, mismatch, or receiver noise can break the cancellation:

    C/N0 = E_s − E_n,   E_n = F_a + 20 log f_MHz − 95.5  dB(µV/m) in 1 Hz   [S, ITU-R P.372, short vertical monopole]

In terms of the transmitter's effective monopole radiated power and L_b this is [E]

    C/N0 = P_emrp [dBm] − L_b − F_a + 177.5.

So a real link reproduces a GUI link of "PathLoss at −164 dBm/Hz" when

    PathLoss = L_b + F_a − 13.5 dB + (transmitting-antenna loss relative to an ideal short monopole)   [E].

The GUI's 180 dB therefore stands for L_b + F_a ≈ 193.5 dB with an ideal transmitting antenna. At 1 MHz at night, where F_a is of the order of 80 dB, that is a basic transmission loss near 113 dB, not 180 dB. At 30 MHz in a quiet rural place, where F_a is near 18 dB, it is about 175 dB. The scenario chapters give the F_a values and their distributions. This conversion assumes the external noise stays above the receiver's own noise after the antenna loss; the chapters say where that fails.

**At 5.8 GHz** a practical EME budget normally uses the system noise temperature, including antenna and receiver contributions at the declared reference plane: N0 = −198.6 dBm/Hz + 10 log T_sys. A system temperature of 80 K gives −179.6 dBm/Hz, about 16 dB below the GUI default. An equivalent total noise factor can still be defined relative to a reference temperature; it is not the external-noise factor F_a alone when receiver contributions are included.

### 3.2 Doppler: shift, drift and spread

| Term | Meaning in this set |
| --- | --- |
| **Shift** | A frequency offset of a path or of the whole signal. |
| **Drift** | Rate of change of a shift, in Hz/s. |
| **Spread** | Width of the Doppler power spectrum of one path or of the composite. Always quoted with its definition. |
| **Inter-path offset** | Constant difference between the shifts of two paths. Two steady paths with an offset beat regularly; this is not spread. |

Spread definitions differ by large factors. For a Gaussian spectrum with standard deviation σ: the ITU-R F.1487 width is 2σ; the full width at half maximum is 1.18 × 2σ; the width holding 80 % of the power is 1.28 × 2σ. For a Lorentzian spectrum of full width at half maximum Γ, the variance does not exist; the width holding 80 % of the power is 3.08 Γ and that holding 90 % is 6.31 Γ [E]. Lunar echoes add a fourth measure, the limb-to-limb width, which bounds the spectrum and is several times its half-power width.

The fading correlation falls to 1/e after about 0.45/B for a Gaussian spectrum of 2σ width B, and after about 0.32/Γ for a Lorentzian [E].

### 3.3 Delay

"Delay spread" is used in the literature for at least four things: the differential delay between two discrete modes (ITU-R F.1487 and the modem's presets); the rms width of a power-delay profile; the width holding a stated share of the power; and the total support (11.6 ms for the Moon). The chapters name which one each number is.

### 3.4 Moderate, adverse, worst case

Each chapter uses **moderate**, **adverse**, and **worst-case** as comparative labels within its stated conditions, not unconditional probabilities. Only call a condition typical or recurring when the source defines an applicable population and supports that frequency. A severe observed episode, a standard test, an extrapolation and a deliberate adversarial combination are different categories. Combining maxima from separate sources does not show they occurred together. Where a source gives percentiles they are quoted with the population and estimator, because a maximum from one event and a 95th percentile are different things. An outage can be arbitrarily severe, so no finite universal worst channel is implied.

## 4. The modem as the channel sees it

### 4.1 Geometry and time scales

DATA is three adjacent carriers. The inner chip rate is Rate/6; the fine chip rate is that times the Spreading factor when Added Symbols is set to Interleave; carrier spacing is 1.25 × the fine chip rate; occupied bandwidth is three spacings (`src/triad_modem_v3.cpp:138-140`). The default pulse is a root-raised-cosine with roll-off 0.25 and a support of ±8 cells; the implementation also offers a tapered RRC (`include/datapump/triad_modem_v3.hpp:17,49-52`). One bit comprises one or more active tiles, each with at least 64 inner coefficients, with burst waits where configured. A slot carries one bit or nothing; two scored quiet slots end a message.

The eight rows of the [manual test list](../../../_tests-RobustModem.md) give the following. Bit lengths, windows and slot spacings are from planner receipts of 2026-11-12, as recorded in section 7.1 of the earlier [resilience record](../../resilience-triple_Robust_Modem/Fable-5.2/robust-nonstationary-channel-research.md) [H]. The waveform geometry source is unchanged since then; the receipts were not regenerated.

| Row | Setting | Cell | Carrier spacing; occupied | Bit | Window / slot spacing | Simulated C/N0 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | Sub-9 kHz: 1 mW, 200 dB, Rate 0.1 Hz, Shift 0 | 60 s | 0.0208333 Hz; 0.0625 Hz | 5,760.4 s (about 96 min) | about 6,767 s / 6,750 s | −36 dB-Hz |
| 2, 3 | Groundwave 500 kHz: 4 W, 180 dB, Rate 1 Hz | 6 s | 0.21 Hz; 0.625 Hz | 576 s | 690–735 s / 690–720 s | 20 dB-Hz |
| 4, 5, 7 | HF 30 MHz: 4 W, 180 dB, Rate 36 Hz, Spreading 10× | 16.7 ms | 75 Hz; 225 Hz | 11.6 s | 26.5–71.5 s / 60–90 s | 20 dB-Hz |
| 6 | HF 30 MHz, hopping | 16.7 ms | 75 Hz; 225 Hz | 257 s (642 dwells) | 272 s / 270 s | 20 dB-Hz |
| 8 | EME 5.8 GHz: 40 W, 200 dB, Rate 360 Hz, hopping | 16.7 ms | 75 Hz; 225 Hz | 26.4 s (66 dwells) | 86 s / 90 s | 10 dB-Hz |

- In row 1 the Shift is zero: the audio waveform is the radio signal, radiated directly at its audio carrier frequency.
- The Helper, where enabled, adds pilots at 550 Hz and at 2,400 Hz or higher and an auxiliary tone at 950 Hz, leading DATA by 38 s (`include/datapump/helper_carrier.hpp:40`, `include/datapump/triad_stream.hpp:15`). At 40 W and 180 dB its total power is at 30 dB-Hz; at 15 kW and 200 dB, 36 dB-Hz [E].
- The high pilot starts at `max(2400, minimum_high, DATA_passband_upper + 450)` Hz and may move upward for the power/leakage guard (`src/helper_carrier.cpp`, placement algorithm). Direct DATA near 8,270 Hz therefore requires a high pilot at least near 8,720 Hz, not 2,400 Hz. Save the actual `helper_plan()`; its returned band and shaping skirts determine whether a sub-9 kHz antenna/band constraint is met. The low pilot and auxiliary remain 550 and 950 Hz.
- Target C/N0, 32 dB-Hz by default, plans the duration. It is separate from the simulated link.

**What these scales mean for a channel.** An effect matters to a row when it changes within one of that row's time scales or across its bandwidth.

| Row | Cell | Pulse support (±8 cells) | Bit T | 1/T | Occupied |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 60 s | 16 min | 96 min | 0.17 mHz | 0.06 Hz |
| 2, 3 | 6 s | 96 s | 9.6 min | 1.7 mHz | 0.6 Hz |
| 4, 5, 7 | 16.7 ms | 0.27 s | 11.6 s | 86 mHz | 225 Hz |
| 6 | 16.7 ms | 0.27 s | 257 s | 3.9 mHz | 225 Hz |
| 8 | 16.7 ms | 0.27 s | 26.4 s | 38 mHz | 225 Hz |

- A statistic that is coherent over a whole bit loses about 1 dB to a drift of 2/T² Hz/s and about 3 dB to a diffuse spread of roughly 1.2/T [E, derived in the resilience record, section 4.3]. For row 1 those are 60 nHz/s and 0.2 mHz. For rows 4, 5 and 7 they are 15 mHz/s and 0.1 Hz. This is a property of whole-bit coherence, not a statement about the present receiver, which adds shorter-coherence, common-frequency and drift alternatives when changing-channel settings are selected ([current architecture](../../../docs/robust-current.md#configured-calendar-reception)).
- Delay matters through frequency selectivity across the occupied band and between DATA and the Helper pilots. For two independently fading paths of equal power separated by τ, the complex channel gains at frequencies Δf apart have correlation of magnitude |cos(πΔfτ)|: 0.5 at Δf = 1/(3τ) and zero at 1/(2τ) [E]. For 7 ms that is 48 Hz and 71 Hz, inside the 225 Hz rows (a fixed two-ray channel instead has notches every 1/τ = 143 Hz). For 12 ms it is 28 Hz and 42 Hz, still far wider than the 0.6 Hz and 0.06 Hz rows. For rows 1 to 3 these delays generally cause little selectivity across each narrow DATA band, but their combined complex gain can still produce amplitude fading or cancellation as well as phase and timing changes. Widely separated Helper frequencies may see different gains.
- Rows 1 to 3 have cells of 6 s and 60 s, so a "fast" channel for them is one that changes in seconds to minutes: day–night transitions, travelling disturbances, weather, tuning drift of a resonant antenna.

### 4.2 What the receiver is told

Peers share a public calendar, keyed ordinal, waveform, key and forward-error-correction mode. The receiver's frequency search follows the oscillator presets times Margin, plus the magnitude of the Doppler field. Live reception needs a declared local noise floor. Unknown-start acquisition is outside the present scope. These contracts are in the [current architecture](../../../docs/robust-current.md) and are not restated here; the chapters refer to them only where a physical effect bears on one, for example a noise floor that is not stationary or a Doppler that is predictable from an ephemeris.

## 5. The existing simulated channel

Read at `0801e5c7` from the [channel-model guide](../../../docs/channel-models.md) and from the configuration structures in `include/datapump/triad_simulation.hpp:22-76` and `include/datapump/triad_modem_v3.hpp:130-287`.

**Presets** (`src/tuning_presets.cpp:20-30`). Each is a pair: an excess-delay value and a Doppler value.

| Preset | Doppler | Delay |
| --- | ---: | ---: |
| None | 0 | 0 |
| Sub9 best | 0.001 Hz | 0.005 ms |
| Sub9 moderate | 0.005 Hz | 0.05 ms |
| Sub9 worst | 0.05 Hz | 5 ms |
| HF best | 0.1 Hz | 0.5 ms |
| HF moderate | 10 Hz | 6 ms |
| HF worst | 30 Hz | 7 ms |
| Aurora best | 10 Hz | 3 ms |
| Aurora worst | 55 Hz | 11 ms |
| EME focused | 6 Hz | 0.5 ms |

Four of these carry the numbers of ITU-R F.1487 test conditions: HF best is its mid-latitude quiet condition, HF moderate its *low-latitude disturbed* condition, HF worst its high-latitude disturbed condition and Aurora best its high-latitude moderate condition. Aurora worst matches the upper ends of the 5 %-exceedance ranges in the DAMSON abstract (2–55 Hz and 1–11 ms; [32](32-skywave-dispersion-highlatitude-equatorial.md)). The Sub9 and EME presets correspond to no standard condition found. The meaning also differs: in F.1487 the Doppler figure is the 2σ fading width of each of two equal Rayleigh paths, and here it is a bound on the offset and wander of a two-path family whose direct path never fades out.

**What a simulated transmission passes through.**

- *Paths.* A direct path and at most one echo. In the GUI's changing-channel realisation the echo delay is drawn once inside the preset's delay; the echo-to-direct amplitude ratio varies smoothly between zero and one; the echo frequency and a common frequency wander vary smoothly inside the preset's Doppler; the direct path never disappears. The knot spacing is a synthetic time scale, not a measured one. Lower-level options add independent random gains on the two rays (Ornstein–Uhlenbeck, or a Gaussian Doppler spectrum of chosen 2σ width), a bounded random delay motion, a diurnal phase term and one attenuation interval with cosine ramps.
- *Selective amplitude perturbation.* Three frequency branches centred on the carriers, each multiplied by a slow and a fast bounded coefficient (sinusoidal or Gaussian-error-function law).
- *Oscillators.* Constant offsets, Wiener phase diffusion, optional white, flicker and random-walk frequency noise, a deterministic transmit frequency slope, and a static Doppler offset.
- *Front end.* Optional converter quantisation and clipping, a one-pole low-pass, one gain step, an overload-recovery gain, one receiver-coordinate tone, and single-sample impulses at a Bernoulli rate.
- *Noise.* White complex Gaussian of fixed variance.
- *Level.* Profile preparation represents the declared transmit power, path loss and noise density through a normalized C/N0 and streaming complex-noise variance; an independent lower-level signal gain is also available. This is a link-ratio convention, not a calibrated physical input voltage.

**What it does not contain.** These are the items the scenario chapters compare with the literature:

- more than two paths, or a path with a specular and a diffuse part;
- general asymmetric or bimodal Doppler spectra, and scattering functions in which delay and Doppler are coupled. The optional proper-complex Ornstein–Uhlenbeck ray gains already have exponential covariance: the corresponding ideal continuous-time spectrum is Lorentzian, while the sampled process has a discrete, aliased spectrum. That mathematical form is not itself calibration to an observed path;
- a received level that follows a physical trajectory (absorption onset and recovery, day–night transition, slow fading with measured statistics, pointing), beyond one ramped interval;
- noise that is impulsive with a measured amplitude distribution, coloured, mains-synchronous or non-stationary; atmospherics with their real waveforms; narrowband and swept interferers other than one tone;
- a deterministic transfer function such as a high-Q antenna system or a ground-wave attenuation function;
- a Doppler shift that follows an ephemeris, with its rate and higher derivatives;
- a single-sideband transceiver's filter group delay, automatic gain control, or a power amplifier's non-linearity.

Absence from this list is not a defect. The present channel is a declared synthetic stress, and several of these effects may turn out not to matter to this modem. The purpose of the list is to make each later comparison explicit.

## 6. Kinds of simulation fidelity

"High fidelity" is not one property. The chapters and the [implementation options](70-implementation-options.md) distinguish five kinds of simulator, in rising order of physical content. None is better in every respect, and a more detailed model is not thereby more realistic.

| Kind | What it is | What it can establish | What it cannot |
| --- | --- | --- | --- |
| **A. Abstract stress** | A bounded synthetic family such as the present presets. | Behaviour under a declared mathematical stress; contrasts between receiver variants. | Anything about how often, or whether, nature produces that stress. |
| **B. Standard statistical model** | A stationary model with standardised parameters, such as the ITU-R F.1487 conditions. | Comparability with published modem results. | Behaviour through transitions, or in conditions outside the model's validated bandwidth, duration and shape. |
| **C. Measurement-calibrated stochastic episode** | A model whose parameters and their evolution are fitted to a named measurement campaign, with the source's estimator. | Behaviour under conditions like that campaign's, with stated uncertainty. | Other paths, seasons or frequencies; anything the campaign's sounder could not resolve. |
| **D. Physics-driven scenario** | Geometry and environment in, channel out: an ephemeris and a scattering law; a ground-wave attenuation function and a ground map; ray tracing through an ionosphere model with an irregularity model; lightning-driven noise. | Internally consistent level, delay and Doppler trajectories; conditions that have not been measured but follow from measured inputs. | The parts the physics model leaves out, which are often the small-scale random ones. |
| **E. Replay** | Recorded channel soundings or recorded noise, passed through or added to the signal. | The exact conditions recorded, including structure no model reproduces. | Statistical confidence beyond the recording; anything the recording bandwidth, dynamic range or duration did not capture. |

Two cautions apply to all five. A simulator can be correct as mathematics and still wrong as physics. And a model that matches the available measurements can still disagree with another equally matching model about what the modem does; when that happens the difference is model uncertainty and has to be carried, not resolved by choosing the more favourable one.

## 7. Reproducible source and evidence contract

The following closes common choices for the mathematical scenarios in 81–85.
These are explicit research fixtures of the canonical native source, not a
claim that they reproduce every GUI/manual row. A physical carrier `fc` is
distinct from the positive digital DATA IF used to construct a public profile;
the channel's RF frequency is `fc + ν` for a baseband offset `ν`. Ideal frequency
translation changes that reference, not the propagation/noise laws.

### Source, calendar and hidden truth

Use the `Format`, `PublicProfileConfig`, `Crypto`, `PreparedTextMessage` and
`Emission` interfaces at the source revision above. Initialize structs to their
declared defaults at that revision, then apply the following explicit overrides;
record the resulting full structs as run inputs so later defaults cannot change
the experiment. No received content selects these settings.

| Item | Reference choice |
| --- | --- |
| Waveform | `IndependentApskV2`; RRC pulse, interleave on, amplitude depth 0.25; minimum inner coefficients 64; sample-rate denominator 1; no burst waits |
| Base sample/IF | 960 samples/s and DATA IF 240 Hz, Helper off, direct complex-envelope injection; valid only when the complete declared source/channel/receiver containment guards pass. It is a translated reference, not the sub-9 kHz direct-audio GUI row |
| Physical-band/Helper comparison | 48,000 samples/s; DATA IF 8,270 Hz for the direct sub-9 kHz branch, 1,725 Hz for the other families. For this comparison set `helper_enabled=true`, `helper.power_w=2`, `helper.data_power_w=1`, `helper.ratio_limit_db=90`, `helper.frequency=helper::Frequency::high`, `helper.callsign="TEST"`, `helper.timecode=true`. Here `power_w` is the total requested budget: effective Helper power is the excess over DATA, capped by the ratio limit; 1/1 would produce zero Helper. These choices specify a synthetic 1:1 Helper/DATA balance, not delivered antenna watts. Native metadata time follows epoch plus dwell time. Use the returned Helper placement/powers and reject an out-of-band antenna or sampled configuration instead of moving tones silently |
| Rate and duration | Sub-9 kHz: rate 0.1, added symbols 1, FHSS off, requested bit duration 16 s; the 64-coefficient constraint produces a 5,760.4 s tile. Groundwave slow case: rate 1, added symbols 1, FHSS off, requested 16 s. HF: rate 36, added symbols 10, FHSS off, requested 16 s, yielding two 11.6 s tiles (23.2 s active geometry). EME: rate 360, added symbols 1, FHSS on, requested 26.4 s, dwell 0.4 s, quiet 0, ramp 0.02 s, margin 0.005 s. Retain the actual returned geometry rather than rounding it |
| Profile timing | Whole-bit waits 0, random extra bits 0, first public slot 0, initial ordinal 0; epoch is the scenario's UTC start rounded down to a multiple of 30 seconds. No calendar key selects the deterministic public schedule. Propagation delays and maximum filter tails are supplied as explicit public bounds by the selected scenario |
| Waveform key | `Crypto::amateur()` at the pinned revision, with its two fixed zero-filled 64-byte roots, and `Format.encrypt=true`; use the same key at both endpoints. A 32-byte `Crypto(span)` is insufficient for that encryption setting. Message mode only; Noise mode and `Crypto::temporary()` deliberately use fresh OS randomness |
| Source message | One uppercase ASCII `A` byte, `0x41`, no newline; `NativeMessageProfile` has FEC off, compression true, content limit 16 bytes and the same key. Use `prepare_text_message` and `wire_bits()`/`bit_source()`; the native short-prefix table gives thirteen bits, `1111101000001`, as a cross-check rather than a second codec implementation |
| Public receiver | Same waveform/key/calendar/ordinal and declared uncertainty bounds; canonical native receiver with the full public hypotheses and W=2H unless an exact allocation is supplied. No realized channel, bit truth, active-slot count, source endpoint or future fade is receiver input |

Use the native bounded deterministic power reference, not the realized message
or received samples, to assign physical power. Let
`mu=Emission.report().data_reference.mean_norm`, `G=report.data_gain`, and
`R_emit=mu*G*G`. The pinned native construction makes R_emit approximately 1
for both Helper-off and Helper-on DATA, while its composite report retains
Helper balance. For DATA reference power C watts multiply the **whole emitted
composite once** by `sqrt(C/R_emit)`; for magnetic RMS amplitude A use
`A/sqrt(R_emit)`. Do not divide the already normalized emission by sqrt(mu)
again. Save the reference's node/tile counts and reported relative standard
error (bounded fixed-bit reference, at most 16 tiles × 2048 nodes), plus actual
energy as a distinct output. A receiver using the raw PatternSource basis must
use `data_basis_gain=physical_multiplier*G` and the matching noise variance;
otherwise physical scaling is not undone by the declared receiver. Helper-on
comparisons keep DATA reference power fixed and add the specified Helper power.

Long sub-9 kHz messages last much longer than six hours. A single active bit is
a component diagnostic; it does not establish a successfully decoded message.
For a whole-message trial the horizon is derived from the actual encoded bit
count and returned public slots, with noise/channel evolution continued through
the final two scored quiet slots and filter/propagation tails. If that trial is
outside a bounded work budget, report it unrun; do not shorten the bit or pretend
one-bit calibration is complete-receiver qualification.

Let `M=wire_bits()`. A clear completion control transmits slots `0,…,M−1`, then
physical silence. The physical completion rule remains two scored quiet slots, normally M and M+1 when their full input windows are uncontaminated. The fixed observation horizon follows [86](86-statistics-and-calculation-checks.md): let q0 be the first public slot whose entire scoring-input window begins after the final nonzero source sample plus the declared propagation-support bound and filter-tail allowance. Render through the full input window of q0+2, including native lookahead: three final quiet opportunities, with one observation margin. Only the evaluator computes that horizon; the receiver never receives M, q0 or the source endpoint. No call turns EOF into success. An interior-fade
counterexample chooses slots `i=floor(M/2)` and `i+1`, retaining later DATA; if
`M<4` in another explicitly substituted codec fixture, choose ASCII `AA` and recalculate.
Set the fade before the earliest contributing source/filter sample for those
quiet-score apertures and keep it through their end. Log the exact sample
ranges, avoiding a fixed “30 minute” substitute. The channel state continues
even during observation gaps; a missing sample is invalid, not valid silence.

Exact ranges come from `transmit_slot(i)` and `slot(i)`, including
`data_support`, `observation`, `quiet_aperture` and adjacent-signal exclusions.
For the sub-9 kHz tile, the halo is approximately 480 seconds on each side.
The exact native integer expression is `ceil(8.L*Fs/fine_chip_rate_hz)+2`
samples; source length is `ceil(bit_seconds*Fs + 2*halo_samples)`. Binary
floating-point rates can move a ceiling by one sample, so neither an algebraic
`480+2/Fs` simplification nor a rounded support duration is authoritative. Save
the returned geometry (at Fs=960 the inspected rate expression gives 460803
halo samples). With Helper and waits off the recurrence
rounds to 6,750 s. The earlier approximately 6,767 s observation receipt is
configuration-specific. See [native calendar](../../../include/datapump/triad_stream.hpp),
[geometry](../../../src/triad_modem_v3.cpp), and
[physical completion](../../../src/triad_message_receiver.cpp).

For this thirteen-bit message, the first two final quiet slots are 13 and 14.
The source's declared spectral support includes pulse margins: with 225 Hz
occupied width and a 0.02 s ramp, `Emission::source()` declares ±362.5 Hz before
channel offsets. Fs=960 leaves 117.5 Hz to Nyquist, not 367.5 Hz. Use 48,000
samples/s for any specified branch that exceeds the 960-sample containment
budget, retaining its physical evolution and all public hypotheses. Set
`nominal_conversion_if_hz` and receiver IF to the same 240 Hz in the translated
base branch; positive IF is required even though emitted I/Q is DATA-centered.
RF oscillator errors use the physical carrier where specified and cannot be
inferred from the IF. These guards are part of the proposed fixture, not a
configure/runtime pass performed here.

### Randomness and numerical decisions

Each stochastic branch specifies a probability law separately from a reproducible
finite seed set. For an initial deterministic comparison use roots `5489+r`,
`r=0,…,19`; these are paired diagnostic realizations, not independently sampled
field episodes or a basis for a natural success probability. A later iid model
trial must independently sample its episode roots under the design in 86.

For a native implementation reference, use `std::mt19937_64` initialized with
`root + 104729*d` modulo 2^64 for domain `d`: 1 path, 2 external noise, 3 pulse
times, 4 pulse marks, 5 equipment oscillator, 6 receiver noise, 7 interference.
Use the engine's upper 52 bits as `(integer+0.5)/2^52`; the midpoint is exactly representable in binary64 and lies strictly between zero and one. Successive pairs feed the
explicit Box–Muller transform for standard normal variates. Do not use a
library-dependent normal-distribution transform. Consumption order is physical
sample/event order within each domain, never thread or chunk completion order.
When a branch requires dependence it shares the specified latent sequence
explicitly; merely reusing seeds is not a physical dependence model. Engine
substreams are deterministic numerical devices, not a proof of statistical
independence. Alternative indexed generators may later be qualified against
the same declared law and paired experiment.

Critical input provenance belongs with each scenario: a source passage/locator,
measurement conditions and estimator for observed values; an equation and inputs
for calculations; a named fixed value or grid for engineering assumptions.
No free “realistic default” completes a missing input. Distinguish numerical
convergence from model discrepancy and empirical validation. Absolute physical
calibration is not supplied by fixing a digital gain.

## 8. How the chapters are organised

Each scenario chapter has the same sections: physical mechanisms; measured and reported values; a moderate-to-worst-case table; how the literature models or simulates the condition; what a high-fidelity simulation would need; contradictions, open questions and things not found; references. The [README](README.md) lists the chapters and summarises their findings.

Equal seed roots do not create identical physical white-noise histories at different sample rates. Pair deterministic signal/channel checks across rates, and compare noise PSD/ENBW separately unless an explicit common-band reconstruction couples the two noise processes.

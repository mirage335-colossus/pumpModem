# Random, time-varying Doppler and delay spread: literature and options for the triple Robust Modem

- **Purpose.** Research record requested on 2026-11-12: what the academic literature supports about reception when real Doppler shift, Doppler spread, delay spread and frequency-selective fading vary randomly between no effect and the operator-configured bound, and possible implementations for the triple Robust Modem.
- **Scope.** Documentation only. No source, test, build or maintained guide was changed, and nothing was measured. Every number that is not quoted from a cited source or from the code is a closed-form estimate made for this document and is labelled [E]. Section 7.1 reads planner receipts that other work recorded on 2026-11-12; they were read, not regenerated.
- **Source revision.** Tree at `8f725cc2`. Uncommitted Fast Modem work was present. Of the files cited here, only `docs/channel-models.md`, `docs/gui-contract.md` and `docs/validation.md` had uncommitted edits; statements about those three guides refer to the working tree as it stood on 2026-11-12. No Robust source file cited here had uncommitted changes.
- **Review.** A draft was checked by two independent reviewers, one against the code and in-house records and one against the literature and the arithmetic. Their corrections are incorporated. Section 7.1 and the passages that cite it were added afterwards and were not part of that review; their arithmetic was scripted and checked against known values, and their code statements were read from the cited lines.
- **Lifecycle.** Originally written in ignored `local-evidence/`; moved at the user's request to `_ref/resilience-triple_Robust_Modem/Fable-5.2/` on 2026-11-13. Cleanup trigger: remove this file once its applicable findings have been distilled into the maintained guides ([channel models](../../../docs/channel-models.md), [Robust operating guide](../../../docs/indexes/robust.md), [current Robust architecture](../../../docs/robust-current.md)). No maintained guide could be edited during this work, so the purpose and trigger are recorded only here; a pointer from a maintained guide should be added when edits are possible.

## 1. Summary

**The concern is justified by the code.** The operator's Doppler/delay-spread setting is meant as a bound, but the receiver uses it as a single position: any nonzero setting adds one assumed second ray at exactly the preset delay and frequency offset, times Margin. The strength of that ray is fitted freely, so a weaker or absent second ray is covered. A second ray anywhere else, random spread of either ray, and frequency drift are not searched. A common shift of the whole signal is searched only as far as the oscillator bounds and the Doppler field reach; the spread setting does not widen that range, which is ±9 mHz or less in the GPS-disciplined manual rows (section 7.1). Simulation applies the receiver's own assumption, a steady two-ray beat that does not change during a run, so no existing test shows what happens between "no effect" and the bound. The few in-house screens with a mismatched receiver are small; the most direct one failed on all 12 paths.

**Live transmissions are already unperturbed.** With Simulation set to No, nothing synthetic is added to the transmitted audio and the receiver still uses the configured bounds. The switch that governs this is Simulation; Live/Duplex only keeps capture open during transmission.

**Two operating points must be kept apart.**

- HF manual rows 4, 5 and 7 use bits of 11.6 s. Their planning target is 32 dB-Hz and their simulated link is about 20 dB-Hz, which leaves roughly 14 to 26 dB more energy than a stable channel needs [E].
- The in-house low-level studies, at 8, 0 and −6 dB-Hz in a retired prototype, used bits of 23 to 70 s with little margin.

Most of the exposures below are cushioned at the first point and bite at the second. The other manual rows lie between and beyond these: bits of 26 s to 96 minutes, and simulated links from −36 to +20 dB-Hz.

**A desk check of the eight manual rows (section 7.1)** adds four findings. They are estimates from recorded planner output, not measurements.

- The GPS-disciplined HF rows search a common shift of ±9 mHz, and their margin stretches that to about ±0.08 Hz at best. Shifts on real HF paths are tenths of a hertz or more, so these rows would miss on the shift alone unless one is declared in the Doppler field. Declaring ±1 Hz multiplies the frequency points by 30 to 100.
- The hopping HF row is coherent over 257 s, and only about 6 % of what it emits depends on the bit. The upper end of ordinary drift uses up all of its 27 dB of margin.
- The EME row has 6.5 dB of margin at its simulated level. Its 6 Hz spread is physically a random width, which the coherent proposal score tolerates only to about 0.1 Hz.
- Row 1's stated link, −36 dB-Hz, is 68 dB below the target its duration was planned for.

**What the literature says about the channel.**

- Real conditions vary on several axes at once: the common frequency shift and its drift, the offset between paths, the random spread of each path, the delay, and the strength of the second path. Passing at both ends of a range does not imply passing inside it.
- On a quiet day, radar records show the spread decorrelating in about three minutes and the shift within a few minutes. Travelling disturbances have periods of 15 minutes to 3 hours with frequency excursions up to about a hertz. Flares add shifts of the order of a hertz for a few minutes and can remove lower-frequency signals for tens of minutes.
- The ITU-R HF test model is stationary, holds the shift fixed, and was validated on 36 minutes of data from one path. No measured drift rate was found, and only one quiet-day record bears on how long the spread stays stationary.
- The presets carry the numbers of ITU-R F.1487 with a different meaning: there, a random fading width per path; here, a constant offset between two steady rays. A 3× Margin on both delay and Doppler goes beyond any measurement located.

**What the literature says about receiving through it.**

- A score that is coherent over the whole bit loses 1 dB to a drift of about 2/T² Hz/s. For an 11.6 s bit that is 14 mHz/s, above the 0.3–7 mHz/s estimated for ordinary disturbances. For a 70 s bit it is 0.4 mHz/s, below most of that range. The same score loses about 3 dB to a random spread of 0.1 Hz at 11.6 s and about 10 dB at 70 s. The proposal stage is such a score and searches no drift. These are estimates, not measurements.
- Under random spread that the receiver cannot know, the time needed per bit grows as spread/(C/N0)². At 20–32 dB-Hz this costs a few decibels at most and does not bind. At 0 dB-Hz and below, with a spread of tens of hertz, the limit is comparable with or longer than the bits used unless the transmitter's peak power is well above its mean. Part of the bound's corner may be unreachable in principle at low levels, and it matters to tell that apart from a search that is merely inadequate.
- Adding channel hypotheses to an averaged admission statistic is statistically cheap, a fraction of a decibel for dozens of hypotheses. The cost is computing. Published tracking loops lose lock at 15–28 dB-Hz, so they are out of reach at the low levels and marginal at the HF rows' level.

**The three carriers.** No published system was found that uses a few adjacent simultaneous carriers to ease a search, so the stated purpose can be judged only by analogy. By analogy with pilot-tone and residual-carrier systems, carriers that fade together can serve as references for one another, which removes the need for fine frequency and phase hypotheses. That requires a delay under about 1–2 ms and a per-cell SNR near 0 dB or better: against a coherent score it costs about 5 dB of evidence at 0 dB and under 1 dB above +10 dB [E]. The SNR condition holds at the HF rows' levels (+2 to +14 dB per cell) and fails at 8 dB-Hz and below. The mechanism tolerates drift and a common phase wander; it does not cancel random spread faster than a cell, and at multi-millisecond delays the carriers do not reliably fade together. The receiver as built does not use it.

**Possible implementations** are set out in section 6, simulation side first: the first step is to let the simulated channel differ from the receiver's bound and to run a few dozen deterministic corner cases (section 7).

**Standing of these conclusions.** Nothing here was measured on the current receiver. The quantitative statements are closed-form estimates applied to literature results that mostly come from other signal levels. The options are not exhaustive and better ones may exist.

**Evidence grades.** [S] standard or recommendation; [P] peer-reviewed; [T] technical report; [G] grey literature; [H] in-house record; [E] this document's estimate or inference. In the reference list, "read" means the supporting passage was read in the source or an author's copy, and "abstract" means only an abstract or index record was seen.

## 2. Terms used in this document

The operator's single "Doppler" number is used for several different physical things in the literature and in this project. This document keeps them apart.

| Term | Meaning here |
| --- | --- |
| **Common shift** | A frequency offset applied to the whole received signal, the same for every ray and carrier. Over an SSB radio link an ionospheric Doppler shift moves all audio carriers by essentially the same amount, because it acts on the radio frequency. |
| **Drift** | The rate of change of the common shift, in Hz/s. |
| **Inter-ray offset** | A constant frequency difference between two propagation paths. Two steady rays with an offset produce a regular beat. The project's records call this *ray-centre separation*. |
| **Spread (width B)** | The bandwidth of the random fading of one path. ITU-R F.1487 reports it as 2σ of a Gaussian Doppler spectrum. The project's records call this *ray width B*. A path with spread B has a coherence time of roughly 0.45/B [E]. |
| **Delay spread** | Here, the differential delay between two paths, as in F.1487 and in the presets. |
| **Ray level** | The power of the second path relative to the first. The presets and the simulated channel use equal power. The receiver fits each ray's gain freely. |
| **Bound** and **realisation** | The bound is the value the operator configures. The realisation is what the channel actually does at a given moment, which may be anywhere from nothing up to the bound on each of the axes above. |
| **Bit length, T** | The active duration of one bit pattern, over which the proposal stage integrates coherently. It is not the slot spacing and not the observation window (section 3.1). |
| **Independent fading cells, D** | The number of statistically independent channel states within one bit, roughly rays × (1 + B × T). |
| **Energy per coherence time, γ** | (C/N0) / B. It decides whether a receiver that does not know the channel can integrate efficiently. |
| **Cell** | One fine chip of the waveform (16.7 ms at a 60 Hz fine chip rate). |
| **Slot** | One public calendar position that carries one bit pattern or nothing. |

## 3. The system as built

Each statement below was read from the source at the revision above. Line numbers refer to that revision.

### 3.1 Waveform, bit length and operating points

- **Three adjacent carriers.** DATA is the sum of three carriers at the DATA intermediate frequency and one spacing either side (`src/triad_modem_v3.cpp:348`).
- **Geometry follows Rate and Spreading.** Inner chip rate is Rate/6. Fine chip rate is that times the Spreading factor when Added Symbols is set to Interleave, and equal to it otherwise. Carrier spacing is 1.25 × the fine chip rate, and occupied bandwidth is three spacings (`src/triad_modem_v3.cpp:138-140`).

  | Configuration | Fine chip rate | Cell | Spacing | Occupied |
  | --- | ---: | ---: | ---: | ---: |
  | HF manual rows 4–7 (Rate 36 Hz, Interleave, Spreading 10×) | 60 Hz | 16.7 ms | 75 Hz | 225 Hz |
  | GUI field defaults (Rate 3.6 kHz, spreading off) | 600 Hz | 1.67 ms | 750 Hz | 2250 Hz |
  | Manual rows 2–3 (Rate 1 Hz, no spreading) | 0.167 Hz | 6 s | 0.208 Hz | 0.625 Hz |

  Unless stated otherwise, worked numbers in this document use the HF manual-row geometry, because those rows carry the "HF worst", "Each 65%", 3× settings.
- **Each cell is a keyed three-carrier pattern.** A keyed permutation assigns the powers 0.4, 1 and 1.6 to the three carriers (six orders); each carrier takes one of eight keyed phases; the Interleave construction multiplies each carrier in each fine cell by a keyed power factor (×0.75 or ×1.25) and sign. The three coefficient powers are normalised to a constant sum per cell (`src/triad_modem_v3.cpp:99-115`).
- **A bit selects one of two independent patterns.** Bit 0 and bit 1 are two independently keyed pseudo-random patterns on the same payload cells. They are not antipodal and not constructed to be orthogonal. Cells outside the payload carry a cover pattern that is the same for both bits (`src/triad_modem_v3.cpp:276-296`). The cover share is about 5 % of the bit in rows 4, 5 and 7, a third in rows 1–3, and about 94 % of what the hopping rows emit (section 7.1).
- **Bit length, observation window and slot spacing are three different things.**
  - The bit's active duration T is a whole number of tiles, chosen by the duration planner from the Target C/N0, which defaults to 32 dB-Hz. For HF manual rows 4, 5 and 7 it is one tile of 11.6 s (`tests/test_triad_fine_session.cpp:107`; [validation](../../../docs/validation.md)).
  - The window scored for one slot is longer, because it spans the Helper lead, the arrival prior and the tails: 71.5 s for row 4.
  - A slot carries exactly one bit pattern or nothing. Successive slots are at least 60 s apart without the Helper and at least 90 s apart with it (`include/datapump/triad_helper_burst.hpp:167-174`).
  - The other rows have longer bits, also one tile each: 26.4 s (row 8), 256.8 s (row 6), 576.4 s (rows 2 and 3) and 5,760.4 s (row 1), from recorded planner receipts (section 7.1).
- **The Helper is a separate three-tone signal.** Pilots at 550 Hz and at 2400 Hz or higher, and an auxiliary metadata tone at 950 Hz, lead DATA by 38 s and continue through it (`include/datapump/helper_carrier.hpp:28-40`, `include/datapump/triad_stream.hpp:15`). Helper acquisition returns frequency intervals only. Its own contract says it supplies no DATA decision, clock estimate or common phase reference (`src/triad_receiver_helper.hpp:9-11`).
- **Message end.** Two scored quiet slots after DATA complete a message (`src/triad_message_receiver.cpp:20,759`).

**Operating points used in this document.**

| Label | C/N0 | Bit length | Energy margin over a stable channel [E] | Per-cell SNR [E] |
| --- | --- | ---: | --- | --- |
| HF manual rows 4, 5, 7 | Target 32 dB-Hz, which sets the bit length. Simulated link about 20 dB-Hz, from 4 W, 180 dB and the GUI's default noise density of −164 dBm/Hz | 11.6 s | about 26 dB at 32 dB-Hz; about 14 dB at 20 dB-Hz | +14 dB; +2 dB |
| Low-level in-house cohorts (retired isolated prototype, explicit noise overrides) | 8 and 0 dB-Hz | 23.2 s | small | −10 dB; −18 dB |
| | −6 dB-Hz | 69.6 s | small | −24 dB |

The Target C/N0 only plans the duration; in simulation the noise follows transmit power, path loss and noise density (`src/robust_profile.cpp:260,342-343`, `src/gui/link_planner_model.hpp:17`). In-house records give 20.02 dB-Hz for this nominal link [H]. In live operation the Target also sets the signal level the receiver assumes (section 3.3). Section 7.1 gives the same figures for all eight rows.

The margin is the ratio of the bit's energy to the roughly 45 nats of evidence that admission needs on a stable channel once the full search grid is charged (section 3.3) [E]. The low-level cohorts ran at known synchronisation with their own gates, so their margins are not comparable in detail.

### 3.2 Coarse search

- **Hypotheses.** Two bits × 13 public epoch alternatives give 26 lanes; a nonzero clock bound triples them with clock offsets of −bound, 0 and +bound (`src/triad_receiver_coarse.cpp:25,283-288`).
- **Frequency grid.** The step is 1/(4T), where T is the whole rendered bit including pulse halos (`src/triad_receiver_detail.cpp:14-24`). The grid spans the full ±frequency bound, about 8 × bound × T points; a grid needing more than 65,536 points is rejected. For row 4 that is 8,596 points across ±90.5 Hz. The bound is the oscillator bounds times Margin plus the magnitude in the Doppler field (`src/robust_profile.cpp:240-244`); the spread preset does not enter it, and in the GPS-disciplined rows it is ±9 mHz or less (section 7.1). A Helper result can replace the grid with at most 64 narrower intervals; with no Helper, or any Helper fallback, the full grid is searched (`src/triad_receiver.cpp:131-133`).
- **Arrival grid.** Every projected sample across the arrival prior is a lag hypothesis. The default prior is about ±7 s (`src/robust_profile.cpp:45-53`).
- **The score is coherent over the whole bit.** For each hypothesis the score is the energy of the observation projected onto one template column, or onto two when a spread is configured, with one unknown but **constant** complex gain per ray, divided by the projected noise variance (`src/triad_receiver_coarse.cpp:749-782,1093`). There is no segmentation, no fading model and no noncoherent combining in this stage.
- **No drift hypothesis in the coarse stage or the deciding lane.** The `Candidate::frequency_drift_hz_per_second` field exists but no receiver code sets it. The diagnostic adaptive lane of the fine stage lets the clock rate move by up to 10 ppm/s between segments, which at a 1.725 kHz carrier acts like a frequency wander of about 17 mHz/s within a quarter of the clock bound [E] (`src/triad_receiver_fine.cpp:28,348-373`).
- **Survivors.** The best two proposals per bit survive, are expanded by half-bound clock offsets, and give at most 12 fine coordinates (`src/triad_receiver_coarse.cpp:26,2023-2069`).
- **Cost.** Search transforms scale roughly as T × frequency bound × (1 + 26 × clocks × rays) × N log N, where the transform length N grows with the bit length plus the configured delay plus the arrival prior (`src/triad_receiver_coarse.cpp:1437-1439`). Configuring any spread doubles the ray factor.

### 3.3 Fine stage and admission

- **Blocks.** The fine stage processes the whole slot observation in 0.25 s blocks grouped in 0.5 s segments (`src/triad_receiver_fine.cpp:68-69`).
- **Channel model.** Each tap gain is a complex first-order Gauss–Markov process with total prior power one, shared equally between taps. Three coherence times, 0.5 s, 2 s and infinite, are mixed with equal weight (`src/triad_receiver_fine.cpp:30,322-328,1105-1109`). These are compile-time constants; no configured channel bound changes them.
- **Assumed signal level.** The unit-power gain prior is tied to an assumed absolute signal level: the simulation's reference gain, or in live operation the Target C/N0 with the declared noise density (`src/robust_profile.cpp:347-355`).
- **Lanes.** The fixed-affine lane keeps each timing state on one time map for the whole slot. The adaptive lane lets timing states move between segments and prunes them. The combined value is the equal-weight average of the two. Only the fixed-affine lane decides; the others are diagnostics (`src/triad_receiver_fine.cpp:1570-1573`; [current architecture](../../../docs/robust-current.md#configured-calendar-reception)). Pruning drops states without renormalising the rest (`src/triad_receiver_fine.cpp:251-270`).
- **Admission is a normalised mixture.** DATA is admitted when the log of the two-bit average evidence, plus the log of the number of fine candidates, minus the log of the full public grid size, reaches ln(216001/10⁻⁴) ≈ 21.49 (`src/triad_receiver.cpp:160-174`, `src/triad_receiver_detail.cpp:9-12,59-62`). The bit is whichever evidence is larger. The winning bit then needs about 38 to 46 nats depending on the row's grid, and about 45 for row 4 [E].
- **Quiet and ambiguous.** If DATA is not admitted, a slot is quiet when its DATA-band energy is below an absolute threshold set from the declared noise variance; otherwise it is ambiguous, and ambiguous slots inside a message become erasures (`src/triad_receiver.cpp:175-180`, `src/triad_message_receiver.cpp:736-756`).

### 3.4 What the channel settings change in the receiver

| Setting | Effect on the receiver | Not affected |
| --- | --- | --- |
| Doppler/delay spread preset, any nonzero value | Adds a second tap at **exactly** preset delay × Margin and preset Doppler × Margin, in both stages (`src/robust_profile.cpp:245-246`, `src/triad_receiver_coarse.cpp:289,495-496`, `src/triad_receiver_fine.cpp:923-924`). Its gain is fitted. Widens projection bands and capture tails. Doubles the planner's minimum mode count. | No grid over other delays or offsets. Coherence times. |
| Additional frequency-selective amplitude modulation | Range-checked only (`src/triad_receiver.cpp:31-39`). Adds spectral skirt and tail allowances. | No per-carrier gain or selective-fading model anywhere in the receiver. |
| Margin (default 3×) | Multiplies clock and frequency bounds and the spread delay and Doppler (`src/robust_profile.cpp:237-246`). | Selective depths, arrival prior, static Doppler. |
| Doppler field (a declared radial velocity or percentage) | Widens the frequency bound by the magnitude of the resulting shift (`src/robust_profile.cpp:244`). It is the only channel setting that widens the common-frequency search. In simulation it also applies that shift (`src/robust_profile.cpp:320`). | — |

Three receiver fields that could describe a random channel exist but are left at zero by the GUI path: `random_doppler_width_hz`, `delay_motion_doppler_bound_hz` and `delay_excursion_seconds`. Even when set, the first two only widen bands, apart from the Helper stage, which uses the width as its assumed pilot line shape (`src/triad_receiver_helper.cpp:445,677`); the third is not read by the coarse or fine stage. The duration planner has a `stochastic_channel` option that would count Doppler width; only one test sets it (`src/triad_duration.cpp:218-220`).

So the configured bound enters the receiver as **one assumed second ray at one position**, with a free gain, not as a range of positions, widths or drifts to be searched.

### 3.5 The simulated channel

- **What the GUI path enables.** A static Doppler offset; the two-ray sum; the sinusoidal selective perturbation; random but constant clock and frequency offsets; Wiener phase noise; a random timing offset; and white noise (`src/robust_profile.cpp:312-343`).
- **The two-ray model is deterministic.** Two rays of fixed gain 1/√2; the second is delayed and rotates at a constant frequency offset. The source describes it as "not an RMS-delay or measured scattering-spectrum model" (`src/channel_spread.hpp:9-12,29-35`). A new seed is drawn for each simulated transmission, so the initial inter-ray phase varies between runs, but nothing varies within a run.
- **It is the receiver's own model.** The simulated delay and offset are the same Margin-scaled values the receiver assumes (`src/robust_profile.cpp:291,321-322`). "HF worst" at 3× simulates 21 ms and 90 Hz, and the receiver models exactly that.
- **The selective perturbation.** Three complementary frequency branches are centred on the three carriers. Each branch's amplitude is multiplied by 1 + (slow depth) × sin(slow) + (fast depth) × sin(fast), with independent starting phases per branch. The fast rate is the spread Doppler × Margin, or 0.1 Hz with no spread; the slow rate is one tenth of it (`src/channel_perturbation.hpp:93-96,149-162`, `src/robust_profile.cpp:253-254`). "Each 65%" sets both depths to 0.65, so the factor ranges from −0.30 to +2.30: each branch passes through zero and briefly inverts. The stage is not energy-normalised and on average adds energy.
- **Richer models exist but the GUI path does not reach them.** `PathEvolutionConfig` offers Ornstein–Uhlenbeck or Gaussian-spectrum random ray gains of chosen width, smooth random delay motion, a diurnal phase term and an outage interval; the selective stage has a bounded random law; the oscillators have drift and flicker options (`include/datapump/triad_modem_v3.hpp:130-139,179-228`). The exporter tool can already generate stochastic channels and channels that differ from a declared spread (`tools/triad-helper-export.cpp`, options `--channel-evolution`, `--random-doppler-width-hz`, `--delay-excursion-ms`).

### 3.6 Live operation and the Live/Duplex control

- **Live transmissions carry no synthetic perturbation.** The channel object is constructed only in the simulation path (`src/live.cpp:784-788`). With Simulation set to No, the emitted waveform is converted directly to audio samples (`src/live.cpp:941-956`). The spread, selective and Margin settings then act only as receiver assumptions.
- **The switch is Simulation, not Live/Duplex.** Live/Duplex decides whether capture stays open while transmitting: unchecked closes capture for the transmission and reopens it afterwards (`src/live.cpp:1204-1209,1236`).
- **This already matches the stated intent** that a real transmission should not receive added perturbations while the receiver still searches the configured bound. The intent was stated in terms of Live/Duplex; as built, it holds for every live Robust transmission regardless of that checkbox. The working-tree [GUI contract](../../../docs/gui-contract.md) says the same of the spread and selective settings: "Declared receiver assumptions in every mode; synthetic effects are injected only in Simulation and explicit loopback."
- **Fast Modem differs.** Its Live loopback test does inject the declared oscillator, spread and selective effects before the audio output, with Margin forced to one ([channel models](../../../docs/channel-models.md), working tree).
- **One wording difference remains.** The GUI contract describes Margin as a public receiver search multiplier. In Robust simulation it also scales the simulated second ray and the selective rate.

### 3.7 Tests

No existing test in `tests/` drives the receiver with a channel whose spread differs from the receiver's configured spread, and none changes a channel parameter during a receiver run. Receiver fixtures either have no spread or synthesise exactly the receiver's two taps with constant gains (for example `tests/test_triad_native_receiver.cpp:49-119`, `tests/test_triad_coarse_native.cpp:103-115`). Time-varying paths appear only in channel-only invariance tests (`tests/test_triad_v3_channel.cpp:57-95`). The nearest exception is a Helper-stage test that assumes a 0.5 Hz line width on steady pilots (`tests/test_triad_native_helper.cpp:152-162`). The manual capacity rows also set the channel equal to the receiver's bound.

### 3.8 What the project's own records already show

These are dated records of isolated studies, mostly with a Python prototype that has since been retired. They do not qualify the current native receiver. They are the closest existing evidence.

| Finding | Evidence and scope | Record |
| --- | --- | --- |
| **Separation is not spread.** A stable inter-ray offset and a random per-ray width are different things with different laws. | Stated repeatedly. Successes at −60 dB-Hz are for stable rays, for rays evolving with a coherence time of 10⁷ s (300 of 300 over 3,000 days), or for known white noise. Coherence of 10⁴–10⁶ s gave no receptions at 750 days. | [spread study](../../../docs/validation-data/triad-v3/2026-11-02-spread/README.md); [−60 dB-Hz study](../../../docs/validation-data/triad-v3/2026-11-02-minus60/README.md) lines 8–13, 188–191 |
| **A receiver assuming larger two-ray geometry than the truth failed.** | Truth 0.5 ms / 0.1 Hz, receiver assuming 1.5 ms / 0.3 Hz: 0 of 12 for all five detectors, against 11 of 12 when matched. Twelve paths, −4 dB-Hz, 120 s; a diagnostic screen. | [diversity record](../../../docs/validation-data/triad-v3/2026-11-01-diversity/README.md) lines 145–146 |
| **Averaging over the relative ray frequency recovered a mismatch case.** | A 10⁻⁷ Hz error in the declared ray Doppler, on observations of hundreds of days at −60 dB-Hz: 4 of 8 with the static receiver, 8 of 8 with a receiver that searches a relative ray-frequency correction with half the prior at zero. The two used different common-frequency grids, so this is not an isolated measurement. | [−60 dB-Hz study](../../../docs/validation-data/triad-v3/2026-11-02-minus60/README.md) lines 194, 231–242 |
| **Delay mismatch lost detections.** | A 0.005 ms receiver delay on a 7 ms path: 8 of 128 against 32 of 128. Conditional draws; the baseline searched only zero arrival within a ±90.5 ms prior. | [spread study](../../../docs/validation-data/triad-v3/2026-11-02-spread/README.md) lines 236–244 |
| **An assumed width wrong by a factor of two lost some detections.** | 73 and 64 of 128 at half and double the true width, against 79 of 128 matched. The record says these small differences do not quantify a mismatch margin. | [spread study](../../../docs/validation-data/triad-v3/2026-11-02-spread/README.md) lines 218–224 |
| **Static coefficients did badly under random fading.** | Zero correct admissions in every tested cell with random width above zero. The record says this correlates with the static basis and does not prove the cause of each miss. | [selective correction](../../../docs/validation-data/triad-v3/2026-11-02-selective-correction/README.md) lines 259–266 |
| **A dynamic block likelihood did much better there.** | At 0 dB-Hz, width 0.5 Hz, no Helper: 26 correct / 1 wrong / 5 missed, against 3 / 0 / 29 for the static receiver. At 8 dB-Hz with width 5 Hz, using 25 ms blocks and a shorter coherence bank: 30 / 0 / 2 against 2 / 3 / 27. Known synchronisation, oscillators off, 32 signals per cohort, bits of 23.2 s, and a diagnostic gate that admitted up to 4 of 32 nulls. | [stochastic record](../../../docs/validation-data/triad-v3/2026-11-05-stochastic/README.md) lines 50–77 |
| **The converse also holds.** On stable paths the flexible short-window model lost. | About 850 days at −60 dB-Hz: static accepted 8 of 8, independent short windows 0 of 8. | [broad study](../../../docs/validation-data/triad-v3/2026-11-03-broad/README.md) lines 314–316 |
| **Flexible gains can absorb the keyed pattern.** | Slower-rate configurations admitted 6 of 32 and 7 of 32 wrong-key signals. Stated rule: "More flexible gains must not absorb the bit pattern." | [selective study](../../../docs/validation-data/triad-v3/2026-11-02-selective/README.md) lines 574–577; [spread study](../../../docs/validation-data/triad-v3/2026-11-02-spread/README.md) lines 376–378 |
| **Random fading costs duration.** | Width 10 Hz needed +5 dB-Hz over about 864 s, and 30 Hz needed +15 dB-Hz over about 120 s, for 64 of 64 (four-path conditional screens at known initial time). A provisional planner forecast 46.4 s / 487.2 s / 34,266 s at 8 / 0 / −10 dB-Hz for an HF case with width 0.5 Hz, against 20.8 s / 83.2 s / 738.4 s for a stable case; the two cases also differ in rate, oscillators, arrival prior and grid size, and only the 8 dB-Hz rows were sampled. | [spread study](../../../docs/validation-data/triad-v3/2026-11-02-spread/README.md) lines 246–249; [duration planner](../../../docs/validation-data/triad-v3/2026-11-06-duration-planner/README.md) lines 27–57 |
| **Wandering phase is unresolved.** | Phase diffusion of 0.001 rad/√s on a 515-day observation cost 5.58 dB against a static channel, and a customised dynamic alternative recovered little (73 and 89 of 512, against a known-channel reference near 99 %). The record adds that the default 0.5 s / 2 s / infinite coherence alternatives "do not cover this useful long channel timescale". Resetting channel priors at each timing update destroyed long-coherent evidence, and a diffuse transition prior drained the adaptive lane. | [gentle tracking](../../../docs/validation-data/triad-v3/2026-11-06-gentle-tracking/README.md) lines 34–43, 157–163 |
| **The pilots help only when timing is resolved.** | At known synchronisation the pilots improved the dynamic receiver (0 dB-Hz: 26 / 1 / 5 without, 32 / 0 / 0 with both). With arrival unresolved, a blind search scored jointly with the pilots gave 3 correct / 2 wrong / 27 missed, and the DATA-only fallback gave 30 / 0 / 2 on fresh captures. The record attributes this to the wide separation of the pilots (1,850 Hz), which makes their relative phase very sensitive to timing, and rules that "coarse pilot scores must not veto the complete DATA path". With 0.5 Hz ray width, real-time Helper acquisition was unresolved on 2 of 2 captures: one failed the onset guard and one exceeded a declared computing guard, which that record says is not a measured decode failure. | [stochastic record](../../../docs/validation-data/triad-v3/2026-11-05-stochastic/README.md) lines 59–68, 118–156; [realtime record](../../../docs/validation-data/triad-v3/2026-11-06-realtime/README.md) lines 123, 149–153 |
| **A wrong noise level dominates false admissions.** | A Gaussian likelihood given half the true noise variance admitted 3,508 of 4,096 null trials. | [diversity record](../../../docs/validation-data/triad-v3/2026-11-01-diversity/README.md) lines 69–71 |
| **A free sideband basis for selective fading was rejected as a default.** | A rank-26 fit needs projected energy 46.68 against 23.92 at rank 2, for about 1.5 dB of recovered energy. | [broad theory](../../../docs/validation-data/triad-v3/2026-11-03-broad/evidence/theory.md) lines 105–119 |

Two gaps stand out. No record tests a receiver whose assumed geometry sits at a bound while the true channel varies randomly below it. And time-varying frequency has been combined with random fading only once, as single admissions, in the duration-planner case with free-running oscillators and a 0.5 Hz width. No record states a purpose for using three carriers.

## 4. What the literature supports

Evidence grades are as defined in section 1. Grey literature here means conference slides, vendor or amateur-radio documents, software source and preprints.

### 4.1 How large the effects are, and how fast they change

**Read the numbers with three cautions.**

- "Doppler spread" is not one quantity. F.1487 and the DRM standard use 2σ of a Gaussian; the DAMSON and Leicester campaigns use the narrowest band holding 80 % of the power; the NTIA reports use a half-width; radar papers use an equivalent width. For a Gaussian the 80 % width is 1.28 times the 2σ figure [E].
- The Leicester campaign statistics exclude signals below 6 dB SNR in 3 kHz, and one of those papers suspects that highly spread signals fall below that floor [Sto11, Sto13]. A modem working far below 0 dB samples conditions those statistics leave out.
- The standards' values are test points chosen by committees, not measured distributions.

**Magnitudes by path class.**

| Path class | Common shift and its variation | Spread | Delay | Sources |
| --- | --- | --- | --- | --- |
| Mid-latitude HF, quiet day | Mean shifts of about 0.3 Hz common; typically within ±0.5 Hz (F layer) and ±0.1 Hz (sporadic E). Two-way radar paths, so a one-way link sees about half | 0.03–0.1 Hz equivalent width with a narrow beam; 0.2–0.3 Hz with a 10° beam (two-way) | Median 0.1–0.2 ms on trough paths | [Jon86, Geo80, Sto11] [P] |
| Mid-latitude trough, night | Not quantified | Median at the 0.5 Hz resolution floor; 95th percentile 1.5–2.5 Hz normally, up to 13 Hz on winter and equinox nights at solar maximum | 95th percentile 1.5–3.6 ms at solar maximum, 0.3 ms or less at minimum | [Sto11] [P] |
| Auroral and polar cap | "Marked swings" as patches cross the path | Typically below 5 Hz with excursions above 20 Hz; 95th percentile 1–13 Hz; 2–55 Hz at 5 % exceedance across four DAMSON paths | 1–11 ms at 5 % exceedance | [Sto13] [P]; [Ang98] [P, abstract] |
| Low latitude | — | 3 Hz exceeded 5 % of the time; a roughly 20 Hz-wide smear in developed equatorial spread-F on one path | 4 ms exceeded 5 % of the time | [F1487] [S]; [Fit93] [T, poor scan] |
| NVIS | Up to about 4 Hz (a cited value, not measured there) | No measurement found; F.1487 disturbed NVIS test value 1 Hz | Mean 0.33 ms, maximum 2.89 ms on one 97 km link; F.1487 disturbed NVIS 7 ms | [Mal21] [P]; [F1487] [S] |
| Sunlit HF path in a flare (single events) | About 1.5 Hz at 6.57 MHz near vertical incidence in one X2.7 event; the peak follows the rate of change of solar EUV within about 20 s. A radar "Doppler flash" lasts 3–4 min (two-way) | — | — | [Chu18, Cha22] [P] |
| MF and LF night sky wave | "Will be small"; about 0.1 Hz excursions | No measurement found | Sky wave up to about 1.2 ms after ground wave; window under 3 ms | [P1321] [S]; [Tro24] [G, preprint] |
| VLF | Propagation "remarkably stable in unperturbed conditions"; no statistic found | — | Modal, with strong dispersion near the 1.6–1.9 kHz waveguide cut-off | [Tho25] [P]; [Suk96] [P, abstract] |
| EME at 5.76 GHz (computed, not measured) | Geometric Doppler, not covered here | Limb-to-limb 86 Hz median and 164 Hz maximum; half-power width 25 Hz median | 11.6 ms total depth; half the power within about 0.2–0.65 ms | [Tay10] [G] |

**How fast things change.**

- **Width and shift decorrelate in minutes even on a quiet path.** In one 26-minute quiet-day radar record the spectral width had a correlation time of about 3 minutes; a separate 12-minute record showed the mean shift decorrelating in a few minutes [Jon86] [P]. Both are two-way measurements. This is the only direct evidence found on how long the spread stays stationary.
- **Travelling ionospheric disturbances** have periods of 15 minutes to 3 hours [Chu18, Aks24, Fri22] [P]. A data paper on crowd-sourced standard-frequency measurements describes the frequency variation as "generally on the order of ±1 Hz" in a figure caption; it gives no statistic [Col23] [P].
- **No measured drift rate was found** in any source. A sinusoidal wander of amplitude A and period P has peak rate 2πA/P. Amplitudes of 0.15–1 Hz with periods of 15–60 minutes give 0.3–7 mHz/s [E]. The upper end assumes a full hertz in a quarter of an hour.
- **Flares.** In one X1.6 event a 3.59 MHz signal, the lowest of three sounding frequencies, was lost for about 20 minutes; in the X2.7 event above the signal stayed above the noise throughout [Chu18]. Radio blackout is described as lasting 30–60 minutes [Cha22]. The flare shift was "not proportional to sounding frequency" [Chu18], so scaling shifts linearly with carrier frequency is only a first approximation. A shift of a few hertz developing over a minute or two would be tens of mHz/s [E].
- **Signal level varies on the bit time scale.** Vendor on-air logs show swings of 10–20 dB on a time scale of about 10 s, with weaker variation over minutes [Kil16] [G]; a conference paper by modem designers states that the standard model does not represent variation in channel quality [Fur09] [G, abstract].
- **Extremes cluster, and campaigns disagree.** On a polar-cap path, more than half of the samples above 7 Hz in one stratum (summer daytime, one frequency) came from two days [Sto13]. On a different path, Svalbard–Kiruna, the spread exceeded 5 % of the time was 22.2 Hz in 1995 and 3.6 Hz in 2003–05 [War06] [P].
- **Disturbed days are common.** Minor geomagnetic storms occur on roughly 900 days per 11-year cycle, and minor radio blackouts on roughly 950 [NOAA] [G]. Mid-latitude spread-F appeared on up to about 88 % of night-time ionograms in the peak month at some East Asian stations and under 15 % at others [Wan19] [P].

**Fading across the three carriers.**

- On a 4470 km transauroral path the envelope correlation bandwidth averaged 4.3 kHz and was at least 1 kHz for 90 % of the time [Aut62] [T]. On that evidence fading is usually flat across 225 Hz.
- F.520-2 lists, as desirable simulator capabilities, selective fades 0.1–1.2 kHz wide sweeping at 0.5–2 kHz/s [F520] [S], so a notch can cross a 225 Hz signal in a fraction of a second.
- For two equal paths the complex gains of two tones Δf apart have correlation |cos(πΔfτ)|. For adjacent carriers 75 Hz apart that is 0.99 at 0.5 ms, 0.89 at 2 ms, 0.16 at 6 ms and 0.08 at 7 ms. The relation is periodic in delay, not monotonic: it is 0.85 again at 11 ms [E].
- So the three carriers share one gain in ordinary conditions and may or may not share it at multi-millisecond delays. The gain-sharing regime changes *inside* the range between "none" and the bound.
- The same record measured random frequency modulation of the received carrier: excursions coincide with fades and scale with the fade rate. It judged this harmless only for shifts above 100 Hz and elements longer than 10–15 ms [Aut62]. A 75 Hz spacing with 16.7 ms cells sits at the edge of that range.

**Fades compared with a bit.** For Rayleigh fading the mean fade duration depends on the spectrum only through its rms width [Tur25] [G, restating Jakes]. With a 0.1 Hz spread a fade below −10 dB lasts about 1.9 s and recurs about every 20 s; with 1 Hz, about 0.19 s every 2 s [E]. At the quiet end a 12 s bit therefore sees at most one or two fades, not an average.

**The presets against these numbers.**

| Preset (delay / Doppler) | Closest literature value | Comment |
| --- | --- | --- |
| HF best 0.5 ms / 0.1 Hz | F.1487 mid-latitude quiet; F.520-2 "good" | Measured quiet channels are narrower still (0.03–0.1 Hz, two-way). |
| HF moderate 6 ms / 10 Hz | F.1487 **low-latitude disturbed** | The label understates it; F.1487 mid-latitude disturbed is 2 ms / 1 Hz. |
| HF worst 7 ms / 30 Hz | F.1487 high-latitude disturbed | Beyond F.1487's own high-latitude 5 % values (5 ms, 25 Hz). |
| Aurora best 3 ms / 10 Hz | F.1487 high-latitude moderate | — |
| Aurora worst 11 ms / 55 Hz | Upper ends of the DAMSON 5 % ranges | From an abstract; a later campaign on one of those paths found far less. |
| EME 0.5 ms / 6 Hz | Delay consistent; computed median half-power spread at 5.76 GHz is 25 Hz | Grey source; 6 Hz corresponds to libration minima. |
| Sub9 (0.005–5 ms, 0.001–0.05 Hz) | No propagation statistic found | A 1.725 kHz carrier radiated directly would sit near the night-time waveguide cut-off [E]. |
| 500 kHz rows use HF best | ITU-R P.1321: steady ground wave plus a slowly fading sky wave | No fitting preset; fades there last minutes, not seconds. |

Two further points follow from the definitions.

- **The numbers match F.1487 but the meaning differs.** F.1487's representative conditions are "two independently fading paths with equal mean attenuation, equal frequency spreads and no frequency shifts", with the spread being 2σ [F1487]. The preset uses the same number as a constant offset between two steady rays. The first is random fading with a coherence time of about 15 ms at 30 Hz; the second is a regular, predictable beat [E].
- **Margin multiplies delay and Doppler together.** At 3× the delay × Doppler product is nine times larger: 0.21 becomes 1.9 for HF worst and 0.6 becomes 5.4 for Aurora worst [E]. No measurement located supports values that large.

### 4.2 Channel models, their limits, and how non-stationary channels are simulated and tested

**The standard model is stationary by construction and was validated on very little data.**

- F.1487 "describes a stationary Gaussian scatter HF channel model" valid for 3 kHz channels [F1487]. In its comparative method "Doppler shift is held fixed (usually zero)".
- The underlying Watterson model was validated on 36 minutes of data: three samples from one day in November 1967 on one 1300 km path, selected because they "seemed most nearly stationary" [VH90] [T, reporting the original]. The authors of that report list its limits as bandwidths of 12 kHz or less, time and frequency stationarity, negligible delay dispersion and a single low-ray path. The bandwidth limit is irrelevant to a 225 Hz signal; the stationarity limit is not.
- The same report quotes the CCIR: "The Gaussian-scatter model almost certainly is not valid for all HF ionospheric channels" [VH90].
- **Spectrum shape is contested.** One later report found measured data "best modeled" by an exponential autocorrelation, which gives a Lorentzian spectrum with heavier tails, while allowing that "both shapes are suitable under different ionospheric conditions" [VH92] [T]. Its comparison data came from one 80 km auroral path and it disclaims quantitative validation. The engine's Ornstein–Uhlenbeck option is the Lorentzian case and its Gaussian option is the Watterson case.
- **The simulator standards do not test a wandering shift.** F.520-2 tests a fixed common offset of 0–10 Hz and lists "frequency drifts 0 to 7 Hz" with no rate, attributing drifts and jumps to radio equipment [F520]. Only the DRM standard gives different constant shifts to different paths, up to 3.6 Hz [DRM] [S].
- **One modem standard does sweep the frequency.** MIL-STD-188-110B Appendix C requires a 9600 bit/s waveform to hold its error rate while the offset sweeps as a triangle between −75 and +75 Hz at 3.5 Hz/s, without fading, at 24 dB SNR [MIL110B] [S, read by a reviewer]. That is a deterministic test of equipment tracking at high SNR, a thousand times faster than the ionospheric drift estimated above. It is a precedent for testing a moving shift, not a model of the ionosphere.
- **The stationary approach is justified by bracketing.** The DRM standard says results on real channels "lead to BER curves between best and worst cases found in the simulation" [DRM]. That is an assumption. Section 3.8 records in-house cases where a channel *milder* than the receiver's assumption did worse.

**What exists for non-stationary channels.**

- **Theory.** A non-stationary channel can be described by a local scattering function, with stationarity assumed only over a finite region of time and frequency [Mat05, Ber14] [P, abstracts].
- **Trend plus stochastic.** In underwater acoustics a measured channel is treated as a slowly varying trend with stationary fluctuation around it, and re-synthesised for laboratory tests [Soc11] [P, abstract].
- **Replay of measured channels.** A public benchmark replays measured time-varying impulse responses, with play times of 33 s to 33 minutes [Wal17] [P, abstract]. Replay was validated against sea trials: packet error rates agreed within 20 % in all but one comparison [Otn13] [P, abstract].
- **State switching.** Finite-state Markov channel models are an established way to represent fading with memory [Sad08] [P, record only].
- **For HF, none of this was found.** No 2018–2026 paper was located that measures a stationarity time for HF Doppler or delay spread, proposes a validated time-varying-parameter HF model, or offers a replay benchmark for HF. Open Doppler recordings do exist [Col23].

**Test length.** F.1487 asks for a run of at least 3000 divided by the Doppler spread: 100 s at 30 Hz, 8.3 hours at 0.1 Hz, about 35 days at 0.001 Hz [F1487, E]. That is incompatible with this project's rule that intensive tests be hierarchical and computationally minimal. Section 7 proposes stratified designs instead, and says what they cannot show.

**Testing a receiver only on its own model.** A receiver evaluated solely on data generated by the model it assumes will look better than it is. The inverse-problems literature calls this an "inverse crime" (Kaipio and Somersalo 2007; cited by title only, the text was not retrieved). The Robust simulation path is in exactly this position (section 3.5).

### 4.3 Fundamental limits

**Energy per coherence time sets what any receiver can do under random fading.**

- For a Rayleigh-fading channel that neither end knows, with a peak-power limit P (in units of the noise density, so P is the peak C/N0), the capacity per unit energy is 1 − I(P)/P, where I(P) is the integral of log(1 + P·S(f)) over the Doppler spectrum S. With the channel known at the receiver it is exactly 1 nat per joule, whatever P. The term I(P)/P is "the penalty for not knowing the channel", and the capacity is approached by on–off signalling with infrequent long bursts [SH05] [P, read in full].
- For a flat Doppler spectrum of width B this gives an efficiency of 1 − ln(1+γ)/γ with γ = P/B, which tends to γ/2 when γ is small. The best possible rate is then about P²/(2B): the time per bit grows as B/(C/N0)², against 1/(C/N0) for a known channel [E, from SH05]. With a fixed peak-to-mean ratio the capacity is quadratic in SNR at low SNR [Set09] [P, read], and the same functional of the Doppler profile appears for channels with delay spread as well [Dur10] [P, read]. The in-house information-bound study reached the same scaling by a different route [H].

  Minimum seconds per bit from that bound, for a Gaussian Doppler spectrum whose 2σ width is B (the F.1487 definition), taking peak power equal to mean power [E]:

  | Spread B | 8 dB-Hz | 0 dB-Hz | −6 dB-Hz |
  | ---: | ---: | ---: | ---: |
  | known channel | 0.11 | 0.69 | 2.8 |
  | 0.1 Hz | 0.13 | 1.1 | 7.7 |
  | 1 Hz | 0.20 | 3.5 | 43 |
  | 10 Hz | 0.78 | 26 | 390 |
  | 30 Hz | 2.0 | 75 | 1200 |
  | 90 Hz | 5.7 | 220 | 3500 |

- **How to read the table.**
  - These are limits on any code and any receiver for the stated channel and peak power. Detection thresholds, search and false-admission control come on top: this receiver needs about 45 nats of evidence per bit against ln 2 for an ideal code, about 18 dB [E].
  - At 20–32 dB-Hz the bound does not bind. For random spreads of 30 and 90 Hz it costs about 0.7 and 1.4 dB at 32 dB-Hz, and about 3.8 and 6.7 dB at 20 dB-Hz; the minimum time per bit stays below a tenth of a second [E].
  - At 8 dB-Hz it leaves room for bits of tens of seconds.
  - At 0 and −6 dB-Hz with a random spread of tens of hertz the bound is comparable with or longer than the 23 s and 70 s bits used in-house, before the 18 dB.
  - As a consistency check, the in-house low-level cohorts sat about 17, 10 and 5 dB above the bound (8 dB-Hz with 5 Hz width; 0 and −6 dB-Hz with 0.5 Hz width), and the dynamic receiver without pilots received 30, 26 and 10 of 32 [H, E]. Those cohorts used a looser diagnostic gate at known synchronisation and differ in other respects, so this shows agreement in order, not a test of the bound.
- **Limits of the limit.**
  - It is exact for a single flat-fading Rayleigh path. For two paths it is the low-SNR asymptote, since spreading power over delay taps does not change the low-SNR result [Set09]; it was not verified as a strict upper bound at finite SNR.
  - It assumes no steady component. A channel with a specular part is easier.
  - It depends on *peak* power. This waveform's peak is above its mean: the three carriers alone can reach 2.8 times the mean if their phases align [E], and an in-house bound puts the absolute peak at about nine times nominal power. A peak three times the mean shortens the 30 Hz row to 0.8 / 26 / 390 s, and nine times to 0.4 / 9 / 130 s [E].
  - It does not apply to a steady two-ray beat, which is deterministic and can in principle be learned.
- **The same quantity appears throughout the literature.** The critical parameter for spread signals is the SNR accumulated in one coherence time [TT00] [P, read]; under a peak limit capacity becomes proportional to that quantity times SNR once it falls below one [ZTM07] [P, read]. The statement that noncoherent operation costs nothing at low SNR holds only with unbounded peak power [Ver02] [P, read].

**Two regimes of fading within a bit.** For an ideal detector that knows the fading statistics, with D equal independent Rayleigh cells per bit and an evidence threshold of 45 nats, the extra energy needed relative to a stable channel is [E]:

| Independent cells D | 1 | 2 | 4 | 16 | 64 | 256 | 1024 | 4096 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| for 50 % detection | 2.0 dB | 1.4 | 1.3 | 2.2 | 3.9 | 6.1 | 8.7 | 11.5 |
| for 90 % detection | 9.2 dB | 5.4 | 3.6 | 2.8 | 3.8 | 5.8 | 8.3 | 11.0 |

- With **few cells** (slow fading, a short bit) the cost is outage: a whole bit can sit in a fade, and high reliability per bit needs many decibels or diversity across bits.
- With **many cells** (fast fading, a long bit) the cost is noncoherent accumulation.
- For 90 % detection the minimum is near twelve cells; for 50 % it is near three. With two rays, twelve cells in an 11.6 s bit corresponds to a spread of about 0.4 Hz.
- These figures assume Gaussian noise of exactly known level and equal-power cells. They are not measurements.

**Whole-bit coherent integration has two hard limits.**

- **Drift.** A linear drift of ḟ loses coherent power as 1 − (π·ḟ·T²)²/180 for small drift, and for large drift spreads the power over ḟ·T² frequency bins [Ran02] [P, read]. The loss reaches 1 dB at ḟ·T² ≈ 2 and 3 dB at about 3.5 [E]:

  | Bit length T | 0.25 dB | 1 dB | 3 dB |
  | ---: | ---: | ---: | ---: |
  | 11.6 s (HF manual rows; 11.9 s as rendered) | 7.2 mHz/s | 14 mHz/s | 25 mHz/s |
  | 23.2 s | 1.9 mHz/s | 3.8 mHz/s | 6.5 mHz/s |
  | 69.6 s | 0.21 mHz/s | 0.42 mHz/s | 0.72 mHz/s |

  Against the 0.3–7 mHz/s estimated in section 4.1 for ordinary travelling disturbances: an 11.6 s bit loses under 0.25 dB; a 23 s bit is marginal at the upper end; a 70 s bit is exceeded by most of the range. Flare-driven drift, tens of mHz/s, exceeds all three. Bits of several hundred seconds, as in the Rate 1 Hz rows, tolerate only microhertz per second, though those are ground-wave configurations where little drift is expected. The coarse stage searches no drift hypothesis (section 3.2).
- **Spread.** Under Gaussian-spectrum Rayleigh fading of width B the expected fraction of fully coherent power after time T is √π·erf(x)/x − (1 − e^(−x²))/x², with x = πBT/√2 [E]:

  | Spread B | T = 11.6 s (11.9 s as rendered) | 23.2 s | 69.6 s |
  | ---: | ---: | ---: | ---: |
  | 0.03 Hz | −0.4 dB | −1.3 | −4.7 |
  | 0.1 Hz | −2.8 dB | −5.1 | −9.6 |
  | 0.5 Hz | −8.9 dB | −11.7 | −16.4 |
  | 1 Hz | −11.8 dB | −14.7 | −19.4 |
  | 10 Hz | −21.7 dB | −24.6 | −29.4 |
  | 30 Hz | −26.5 dB | −29.4 | −34.2 |

  This is the average loss of a whole-bit coherent score against diffuse fading. It applies to the coarse proposal score. A specular path does not suffer it.
- **What the margins imply [E].** With 14 dB of margin the proposal score of an HF manual row stays above its requirement up to a diffuse spread of roughly 1 Hz, and with 26 dB up to roughly 10–30 Hz. That is only the proposal stage: the fine stage's shortest coherence time is 0.5 s in 0.25 s blocks, so for spreads above about 1–2 Hz it has no fitting hypothesis, and what it then does is unknown. At the low-level operating points there is no such margin, and the losses in the table translate directly into missed proposals.

**Identifiability is a separate, narrower point.** A time-varying channel can be identified from a probe, in the absence of noise, only if the area of its delay–Doppler support is below one [Kai62, Bel69, PW06] [P, abstracts]. When the support is not known in advance the condition for every channel is an area of at most one half, and below one for almost all [HB13] [P, read]. These are statements about *learning the channel*, not about detecting a signal: a detector that sums energy needs no identification. Conventions for the area differ by a factor of four between authors, real HF channels occupy far less than one, and a two-ray channel has no support area at all. The delay × Doppler box of the HF-worst and Aurora-worst presets at 3× Margin is 1.9 and 5.4 [E]. The only consequence drawn here is that coherent, per-realisation channel estimation cannot be expected to cover every channel inside such a box. Estimation there has to rely on structure, such as a few rays of small width, or give way to energy statistics.

**Energy statistics have a floor set by noise-level uncertainty.** If the noise power is known only within ±x dB, a detector that compares received energy with the assumed noise level cannot work below an "SNR wall" however long it observes, and coherent processing lowers the wall only in proportion to the coherent block length [TS08] [P, abstract; formulas read in the first author's thesis]. Translated to this receiver [E]: at −6 dB-Hz a 0.5 s coherent block needs the noise density known to about ±0.3 dB, a 2 s block to about ±1.1 dB, and a per-cell statistic to about ±0.01 dB. At 20 dB-Hz a per-cell statistic tolerates about ±3 dB. A whole-bit coherent score is far above any plausible wall. The in-house result that a wrong noise variance dominated false admissions (section 3.8) is the same effect.

### 4.4 Detection and search under unknown, time-varying Doppler at fixed compute

**What the present statistics are, in the literature's terms.**

- The coarse score is a matched-subspace detector. Such a test is optimal only among tests that put no prior on where in the subspace the signal lies, and its statistic carries as many noise degrees of freedom as the subspace has dimensions [SF94] [P, read]. A rank-2 score therefore pays a penalty when the truth is rank 1. Recomputing gives 0.4–0.7 dB [E], and reproduces the in-house table [H]. This is the whole cost of assuming a second ray that is absent or weak.
- For a channel that fades randomly with known statistics the optimum is instead the estimator–correlator: correlate with an estimate of the channel output [Kai60] [P, abstract]. The fine stage's Gauss–Markov model is of this kind.
- Averaging power over all phase histories whose step between adjacent blocks is bounded gives a statistic with an exponentially decaying block-correlation kernel, which interpolates continuously between fully coherent and semi-coherent detection [Der10] [P, read]. That is the same kernel as a first-order Gauss–Markov gain, so the fine stage is a recognised bridge between the two.

**"Design for the bound" is supported for estimating a channel, not for detecting a signal.**

- A basis for a block-fading channel can be designed from only the maximum Doppler bandwidth and the block length [ZHS05] [P, read]. The same group then replaced the single maximum-Doppler subspace with a bank of subspaces at fractions of the maximum, chosen per block [ZMF06] [P, read]: a bound treated as a range.
- A widely repeated statement that an estimator designed for a larger spread than the truth "degrades only slightly" could be confirmed only as far as the abstract of [Li98], which says the proposed estimator is insensitive to channel statistics. It concerns estimation error at positive SNR.
- Minimax-robust design guarantees the worst case over a class, not small loss at its benign members [VP84] [P, abstract].
- No source was found that quantifies the detection loss of designing for the bound. A low-SNR argument gives about 5·log10(N) dB, less an offset, when a detector assumes N independent cells and the channel is in fact coherent [E]; an empirical rule for noncoherent integration in satellite-navigation receivers has the same form, 10·log10(√K) − 5.5 dB for large K [Bor08] [G, thesis]. The in-house 850-day result (section 3.8) is an instance.

**Averaging is valid and cheap in evidence; maximising needs care.**

- A weighted average of likelihood ratios against a fully specified noise law keeps its false-alarm guarantee under any dependence, provided the set and the weights are fixed before seeing the data [VW21, Ram23] [P, read].
- A threshold on the running maximum of evidence accumulated sequentially for one hypothesis is also valid, but that maximum is not itself a quantity that can be averaged or carried forward [Ram23, Sha11] [P, read]. A maximum over a grid, less the log of the grid size, is valid but conservative [E].
- A statistic maximised over a continuous parameter has a tail that grows with the threshold itself, not only with the number of independent regions [GV10] [P, read].
- A Bayes factor with a physically sensible prior is more powerful than the maximised statistic, though by a small margin in the study read [PK09] [P, read].
- **Consequence for this receiver [E].** Admission is already such an average (section 3.3). Adding a channel hypothesis with a predeclared weight w costs at most ln(1/w) nats. Against about 45 nats, eight equally weighted hypotheses cost about 0.2 dB and sixty-four about 0.4 dB. The in-house gentle-tracking and −60 dB-Hz records use the same bound [H]. Hypotheses are cheap in sensitivity. Their cost is computing.
- **Two cautions.** A Bayes factor against a *composite* null is generally not protected in this way [Gru24] [P, read], and the receiver's null depends on a declared noise level. And refining only the top coarse candidates is a data-dependent selection; the receiver covers this by charging the full grid, and any new lane must keep doing so.

**Semi-coherent search helps at fixed cost only where coherent cost explodes with integration time.**

- The gains reported in gravitational-wave and pulsar searches exist because a coherent search over many parameters cannot afford to use all the data; a neutral review says exactly that [Wet23, PS12, BC00, PC14] [P, read]. One paper's conclusion that the semi-coherent search is always the more sensitive rests on a coherent cost growing as the sixth power of the coherent time, with an advantage growing only as the eighth root of the time ratio [PC14].
- The familiar N^(1/4) law is a weak-signal, large-N approximation that its own authors say "is often not well satisfied", worst at small false-alarm probability and N up to about a thousand [PS12]. That is this receiver's regime, which is why the table in section 4.3 was computed from the actual distribution.
- Here the coherent search already covers the whole bit. The literature read gives no reason to expect a *computing* gain from semi-coherent processing. Its case in this receiver is robustness to spread and drift.

**Tolerating a wandering frequency.**

- Hidden-Markov (Viterbi) tracking over short coherent blocks is cheap and robust, and less sensitive: 41 of 50 test signals found against 50 of 50 for the most sensitive method, at about a thousandth of the computing. The test signals did not wander, and forcing a no-wander model did not recover the misses [Suv16] [P, read].
- Enforcing phase continuity between blocks recovered about 1.5 times in amplitude at about ten times the cost, and only when phase was marginalised; per-block phase estimates gave nothing [Mel21] [P, read].
- Tracking the frequency derivative as a second state gained "a factor of a few" for 100–1000 times the cost [Sun18] [P, read].
- The design rule in that literature is a coherent block no longer than the time in which the frequency moves one bin, (2·|ḟ|)^(−1/2) [Sun18]. For 2 mHz/s that is 16 s; for 50 mHz/s, 3 s [E].
- An extra search dimension costs even when the signal does not need it: adding a second-derivative search made one real pulsar search about 80 times slower with 41 times the trials, and unaccelerated pulsars lost 0.7–0.8σ of significance [AR18] [P, read].
- Path-maximised statistics need extreme-value analysis for their noise-only distribution [JK02] [P, abstract], and dynamic-programming track-before-detect degrades rapidly below some SNR, sometimes not improving however many frames are added [TE96] [P, abstract]. A sum over paths avoids the first problem; a maximum conflicts with the false-admission accounting.

**Closed-loop tracking has thresholds between the two operating points.**

- For the Mars rover landing signal a phase-locked loop would have needed about 22 dB-Hz; open-loop search over frequency and frequency rate with short FFTs worked about 6 dB lower. The lowest carrier level tested was about 5.5 dB-Hz, with 1 s coherent blocks summed for 10–20 s and three rate hypotheses, and with markedly more tracking errors there [Sat03] [T, read].
- Under ionospheric scintillation, phase loops slip at 25–28 dB-Hz (15 Hz bandwidth), about 20 dB-Hz (3 Hz), and 15–21 dB-Hz for Kalman-filter loops; inside a deep fade the phase is untrackable at any bandwidth [Hum05] [G, conference, read].
- An adaptive predictor bank proposed for the same landing problem was tested only from 10 to 30 dB-Hz, with loss of lock dominating at the low end [Lop10] [P, read].
- So closed-loop tracking is out of reach at 8 dB-Hz and below, and marginal at the roughly 20 dB-Hz of the HF manual rows' simulated link.
- Over-the-horizon radar corrects ionospheric "frequency modulation" within its coherent integration time, which is the closest precedent to estimating the wander from the signal itself [PB88, HC93, Bou87] [P, abstracts]. It works from sea clutter that is strong within the wander's own time scale, with smooth deterministic models; in the one open paper read the reference had roughly 19 dB of SNR per analysis window [Li14] [P, read; arithmetic E]. It is not a precedent at low signal levels. The same literature notes that radar sea-state work needs 50–100 s of coherent integration and therefore relies on unusually stable sporadic-E paths [Geo80].

**An operational decoder that treats spread as a range.** The WSJT-X Q65 decoder computes symbol likelihoods with an estimator–correlator weighting for an assumed Doppler spread, and tries a geometric ladder of seven to eleven spread values in turn until one decodes. It does not average; a checksum validates the winner. Its authors assume about 8 dB of threshold degradation at the largest spread [Q65] [G, source code and guide read]. A receiver without an error-detecting check cannot copy "first success wins" safely.

**Reducing average work without dropping hypotheses.** Coarse-to-fine testing is provably optimal for *mean* computation when no stage misses a true signal and the no-signal case dominates [BG05] [P, read]. Sequential tests are usually truncated to bound the dwell [Tan95] [P, abstract]. Nothing read bounds worst-case computing, which is what a real-time ceiling needs.

**Recent work that might have changed the picture.** Learned detectors for the continuous-wave problem approach matched-filter sensitivity in restricted settings and are not reported to exceed it [DP20, JP23] [P, abstracts]. Sparse-Bayesian delay–Doppler estimators are evaluated as channel estimators with pilots 30 dB above the data [Wei21] [G, preprint read]. No 2018–2026 paper was found demonstrating acquisition under random Doppler at or below 0 dB-Hz.

**Not found.** A closed-form coherent loss under random-walk frequency; a quantified detection loss from designing for the bound; the detection gain and cost of a Bayesian mixture over coherence times at negative SNR; a worst-case computing analysis of a top-K candidate cut.

### 4.5 The three carriers

The stated purpose of the three separate carriers is to make the receiver's search computationally feasible under turbulent conditions. No maintained record gives the reasoning (section 3.8), and no published system was found that uses a few adjacent simultaneous carriers with keyed relative powers and phases for this purpose near or below 10 dB-Hz. That is an absence within a bounded search; the bispectrum, multi-frequency radar and optical self-coherent literatures were not searched. The purpose can therefore be judged only by analogy, mechanism by mechanism.

**Channel arithmetic for the HF manual-row geometry** (75 Hz spacing, 16.7 ms cells) [E]:

| Bound (delay / Doppler) | Adjacent-carrier gain correlation, at the preset / at 3× Margin | Spread × cell | Energy per coherence time γ at 20 / 8 / 0 / −6 dB-Hz |
| --- | ---: | ---: | --- |
| HF best 0.5 ms / 0.1 Hz | 0.99 / 0.94 | 0.002 | 1000 / 63 / 10 / 2.5 |
| EME 0.5 ms / 6 Hz | 0.99 / 0.94 | 0.10 | 17 / 1.1 / 0.17 / 0.04 |
| HF moderate 6 ms / 10 Hz | 0.16 / 0.45 | 0.17 | 10 / 0.63 / 0.10 / 0.025 |
| HF worst 7 ms / 30 Hz | 0.08 / 0.23 | 0.50 | 3.3 / 0.21 / 0.033 / 0.008 |
| Aurora worst 11 ms / 55 Hz | 0.85 / 0.08 | 0.92 | 1.8 / 0.11 / 0.018 / 0.005 |

The two-path correlation is periodic in delay, so it does not fall steadily: it is high again at 11 ms. The signal-to-noise ratio in one cell, summed over the three carriers, is +14 dB at 32 dB-Hz, +2 dB at 20 dB-Hz, and −10, −18 and −24 dB at 8, 0 and −6 dB-Hz [E].

**Mechanism 1: the carriers as a reference for one another.**

- *What the literature supports.* A reference sent with the data and through the same channel lets a receiver remove the channel's common phase and frequency. Examples: a pilot tone inside the data spectrum for mobile satellite links [Dav87] [P, abstract; patent text read]; an unmodulated Doppler-correction tone in the standard 16- and 39-tone HF modems [MIL110B] [S, read]; a residual carrier beside the data tones in the Mars landing signal, where the common Doppler is estimated on the carrier and then removed [Sat03]. Underwater links remove one common Doppler factor by resampling and then find the residual offset by a one-dimensional search on null subcarriers [Li08] [P, read].
- *Conditions in those sources.* Reference and data must experience the same degradation [Dav87], so their separation must be well inside the coherence bandwidth [E]. The reference needs adequate SNR in a bandwidth about twice the Doppler. Every demonstration is at 9 dB-Hz or more in total, most near 35 dB-Hz.
- *Limits.* Multiplying two noisy observations costs 3 dB because half the energy is in the reference, 3 dB more because the reference is noisy, and adds a noise-times-noise term that dominates at low input SNR [TM04] [T, read]. Weak-signal sums of short coherent pieces grow as the *square* of the per-piece SNR [Low99] [T, read].
- *Applied here [E].* The three carriers always share the common shift, and they share the fading gain when the delay is below about 1–2 ms. A per-cell statistic then exists that is unaffected by the common phase: project the three-carrier cell onto the keyed pattern and take the squared magnitude. It needs the frequency only to a fraction of the chip rate, a grid roughly 700 to 4,000 times coarser than 1/(4T) for bits of 12 to 70 s, with no sensitivity to clock or drift inside a cell. That is a large reduction in search. Its price is evidence. Measured by the mean evidence collected per cell, with the amplitude known and the phase not, it gives up about 0.4 dB to the coherent score at a per-cell SNR of +14 dB (32 dB-Hz with 60 Hz chips), 3.6 dB at +2 dB (20 dB-Hz), 5 dB at 0 dB, 13 dB at −10 dB (8 dB-Hz) and 21 dB at −18 dB (0 dB-Hz). Section 7.1 gives the figure for each manual row.
- *Verdict.* Consistent with the stated purpose, by analogy, where the carriers fade together and the per-cell SNR is near 0 dB or better. Those are the less turbulent conditions. It removes the need to follow a common shift, drift or phase; it does not cancel random spread faster than a cell.

**Mechanism 2: frequency diversity.**

- *What the literature supports.* Combining independently fading branches without knowing their phase reduces outage, and there is an optimum number of branches: at fixed total energy the error probability first falls and then rises towards one half as the energy is split further [TT00, Sta85] [P, read]. With the optimum order, noncoherent FSK on Rayleigh fading loses about 5.25 dB against no fading; the best codes reduce the loss to 1.35 dB [Sta85]. The optimum corresponds to a mean SNR of roughly 3 per branch [E, from an exponent in the same author's course notes [StaN]; the textbook statement was not retrieved].
- *Conditions.* The branches must fade independently, and each needs several decibels of SNR after its own coherent integration.
- *Applied here [E].* Adjacent carriers are 0.99 correlated at 0.5 ms and 0.89 at 2 ms: no diversity in ordinary conditions. They can decorrelate at several milliseconds. There, at the low-level operating points, the energy per coherence time is far below the optimum, so splitting it three ways costs sensitivity.
- *Verdict.* Not supported for these spacings in ordinary conditions, and not helpful at low levels where the carriers do decorrelate.

**The first two mechanisms need opposite conditions.** Mutual reference needs the carriers to share a gain; diversity needs them not to. The same three carriers cannot provide both, and which regime applies depends on the realised delay, which changes inside the range the bound allows.

**Mechanism 3: evidence in the power pattern.**

- *What the literature supports.* When fading is too fast for phase to carry information, the information-bearing quantity is amplitude: the capacity-achieving input for memoryless Rayleigh fading is discrete with a mass point at zero [AF01] [P, abstract], and under a peak limit the low-SNR optimum is on–off keying of unit-modulus sequences [Set09]. An in-house bound says the same: independent per-cell gains erase phase-only distinctions while amplitude differences survive [H].
- *Limits.* The theory recommends on–off signalling with long off periods and long coherent bursts, not a modest power permutation in every cell.
- *Applied here [E].* Evidence from the 0.4 / 1 / 1.6 permutation alone is about 0.16·T·(C/N0)²/(chip rate): 21 dB below coherent detection at 8 dB-Hz over 23 s, but ample at 20 dB-Hz and above.
- *Verdict.* It is the evidence that survives the fastest fading and does not need the carriers to share a phase. It is weak at low C/N0 because the power contrast is mild, and it is what the selective amplitude stress attacks directly.

**Mechanism 4: a multi-tone signature.**

- *What the literature supports.* A known tone pattern can be found at lower SNR than data can be decoded: one amateur mode synchronises 93 % of the time at a level 4 dB below its 41 % decode point [Tay05] [G, read].
- *Limits.* Splitting energy over k tones that are combined without phase lowers weak-signal detection by √k, 2.4 dB for three [E, from Low99]. Simultaneous tones raise peak power on a peak-limited transmitter, which is one reason serial-tone HF modems displaced parallel-tone ones [JM99] [T, read]. Published systems get enough energy per tone from long symbols, 0.15 to 11 s, not 17 ms cells.
- *Verdict.* It helps false-alarm rejection once enough energy is integrated. It does not shrink the frequency grid of a coherent search, whose resolution is set by the coherent duration and not by the number of carriers [E].

**Mechanism 5: a pilot beside the data.**

- *What the literature supports.* With a data-free pilot component, coherent combining of pilot and data beats squaring and adding them, at 25–35 dB-Hz [BL08] [P, read].
- *Limits.* At low SNR, schemes that estimate the channel from training and then trust the estimate are highly suboptimal: their achievable rate falls as the square of SNR and half the energy goes to training [HH03] [P, read]. Training needs far longer coherence than joint processing [ZTM07]. Pilot-assisted transmission was 1.2–1.8 dB worse than noncoherent transmission for short packets even at moderate SNR [Ost19] [P, read]. In the Mars landing signal the pilot was the weak link: carrier acquisition under dynamics needed 19–22.5 dB-Hz while the tones needed about 13 dB-Hz [Sat03].
- *Applied here.* Each hypothesis is already a bit-long known pattern, so the whole signal acts as its own pilot. The Helper pilots lie about 1.2 kHz below and at least 0.7 kHz above the DATA carrier, and 1.85 kHz from each other; the in-house record attributes the failure of joint pilot scoring under unresolved timing to that separation [H].
- *Verdict.* Little to gain.

**An observation on cell shape.** A rule for multicarrier grids on dispersive channels sets the ratio of symbol time to carrier spacing equal to the ratio of delay spread to Doppler spread [JW07] [P, read]. With Rate 36 and 10× the cell's ratio is 2.2 × 10⁻⁴ s², against 2.3 × 10⁻⁴ for 7 ms / 30 Hz and 2.0 × 10⁻⁴ for 11 ms / 55 Hz, but 5 × 10⁻³ for 0.5 ms / 0.1 Hz [E]. So that geometry is shaped like the turbulent bounds, not the benign one. The rule minimises self-interference at high SNR, and at those bounds the delay and Doppler are 40–70 % of the cell's time and spacing, far beyond the regime in which such grids are designed [MBH13] [P, read]. Whether the match was intended is not recorded, and it should not be read as intent.

**Overall.**

- By analogy with systems at higher signal levels, three carriers can ease a search when they are used as a self-contained block per cell that needs no fine frequency or phase hypothesis. The conditions are that the carriers fade together and that the per-cell SNR is near 0 dB or better.
- At the HF manual rows' levels the SNR condition holds. Whether the carriers fade together depends on the realised delay.
- At 8 dB-Hz and below the mechanism is weak, and nothing in the literature read offers a substitute that three adjacent carriers could provide.
- The receiver as built does not use the mechanism. It uses the three carriers as one coherent template with a shared gain per ray, which is the most sensitive use when the channel is coherent.
- These conclusions rest on this document's estimates applied to literature results from other regimes. No prior art was found to test them against, and they should be checked (section 7) before being relied on.

### 4.6 How comparable robust waveforms cope

Conversions: SNR in 3 kHz + 34.8 dB, or in 2500 Hz + 34.0 dB, gives C/N0 in dB-Hz [E].

| System | How it copes | Documented conditions | Source |
| --- | --- | --- | --- |
| MIL-STD-188-110B, 75 bit/s serial tone | Walsh-coded spreading across 3 kHz with known probe segments | *Required:* bit error rate 10⁻⁵ at 2 dB SNR in 3 kHz (about 37 dB-Hz) on two Rayleigh paths, 5 ms / 5 Hz | [MIL110B] [S, read] |
| STANAG 4415 | Same waveform, harder targets | Described as designed to tolerate, as a minimum, spreads up to 10 ms and 50 Hz [Sto13]. The performance table was **not verified**: the standard is not public | [Sto13] [P]; vendor page [G] |
| MIL-STD 39-tone parallel modem, 75 bit/s | Time-differential QPSK per tone, an unmodulated Doppler tone, in-band diversity | 0–4 dB in 3 kHz at 2 ms / 2 Hz | [MIL110B] |
| Q65 (WSJT-X) | 65-tone FSK, strong code, 22 sync symbols; energy metric matched to an assumed spread, tried over a ladder of spreads | About 6 dB-Hz with no spread (60 s); about 5–11 dB-Hz at 20 Hz spread depending on submode and prior information; 11–13 dB-Hz at 100 Hz | [Q65] [G; figures read by eye] |
| FST4W (WSJT-X) | 4-tone FSK with block detection over minutes | −10.8 dB-Hz over 30 minutes, but only with spread below 0.089 Hz | [FST4] [G, read] |
| FT8 (WSJT-X) | 8-tone FSK, 12.6 s | 13 dB-Hz without fading; 12 dB worse at 10 Hz spread | [FT8] [G, read] |
| Mars landing tones | Residual carrier plus one of 256 tones for 10 s; open-loop search for the carrier, then tone detection | Tones at about 13 dB-Hz once the carrier is found; carrier under dynamics 19–22.5 dB-Hz | [Sat03] [T, read] |
| MT63, DominoEX (amateur) | 64 simultaneous differential tones; frequency-difference keying | Authors' statements only. MT63's describer rates it a poor performer when Doppler acts within its 100 ms symbol | [Gre] [G] |
| Underwater OFDM | Remove one common Doppler factor by resampling, then a one-dimensional search using null subcarriers | Worked to 10 knots; SNR adequate for QPSK | [Li08] [P, read] |

What transfers, and what does not [E]:

- **The weak-signal modes keep the energy in each noncoherently detected symbol at several decibels**, by long symbols, and get reliability from coding across many symbols. They send one tone at a time. The parallel-tone modems are the exception, and were displaced partly for peak-power reasons [JM99].
- **Spread is handled by making the tone spacing at least as large as the spread**, or by weighting neighbouring bins for an assumed spread. Tolerance costs sensitivity: about 6 dB between 0 and 100 Hz for Q65.
- **The WSJT modes validate each decode with a checksum.** This receiver instead admits each one-bit slot on an evidence threshold.
- **None uses simultaneous adjacent carriers to shrink a search.**
- **Energy per bit.** Q65's most robust submode decodes a 77-bit message at about 8 dB-Hz with a 20 Hz spread, about 9 dB of energy per bit. One bit in 23 s at 8 dB-Hz is about 22 dB per bit. The comparison is not like for like: those are 50 % decode thresholds validated by a checksum, without a search over ±90 Hz and ±7 s and without a 10⁻⁴-per-day admission budget.
- **Operation below 0 dB-Hz is documented only for nearly static channels** and transmissions of many minutes.
- On high-latitude paths the 75 bit/s waveforms kept roughly the same availability through a geomagnetic disturbance that degraded a 2400 bit/s waveform [Jod01] [P, abstract]. That is an availability estimate from channel measurements and simulator-characterised modems, not an on-air trial.

### 4.7 Contested and qualified findings

| Common statement | What the sources read actually support |
| --- | --- |
| The HF channel has a Gaussian Doppler spectrum. | Assumed by the standards. One report found a Lorentzian shape fitted measured data best, then allowed that both fit under different conditions [VH92]. The CCIR itself doubted the Gaussian model's generality [VH90]. |
| "Doppler spread" figures can be compared between sources. | Several definitions are in use (section 4.1). |
| High-latitude paths have spreads of tens of hertz. | 22.2 Hz and 3.6 Hz at the 5 % level on the same path a decade apart [War06]; extremes cluster on a few days [Sto13]. |
| Real results fall between the best and worst simulated cases. | An assumption in the DRM standard [DRM]. In-house, a channel milder than the receiver assumed did worse (section 3.8). |
| Doppler shift scales with carrier frequency. | A first approximation. Flare-driven shifts were "not proportional to sounding frequency" [Chu18]. |
| Semi-coherent search is more sensitive than coherent search at equal cost. | Only where coherent cost rises steeply with integration time [Wet23, PC14]. |
| Combining N segments without phase loses N^(1/4). | A weak-signal, large-N limit that is "often not well satisfied" [PS12]. |
| Viterbi tracking found 41 of 50 against 50 of 50, the price of tolerating wander. | The test signals did not wander. The gap reflects shorter coherence and a thousand-fold lower computing cost [Suv16]. |
| An estimator designed for the largest spread loses little when the spread is smaller. | Verified only as an abstract's claim of insensitivity [Li98]; it concerns estimation at positive SNR, not detection. |
| Not knowing the channel costs nothing at low SNR. | Only with unbounded peak power and vanishing duty cycle [Ver02, TT00]. With a peak limit the penalty is set by the Doppler spectrum [SH05]. |
| The cost of not knowing the channel is the factor (1 − 1/T). | That is a high-SNR result [ZT02] [P, read]. At low SNR the governing quantity is SNR per coherence block [ZTM07]. |
| More carriers or more spreading give more diversity and so more robustness. | With unknown fading and fixed power, spreading energy thinly lowers both information and detection performance [TT00, Sta85]. |
| Delay–Doppler waveforms (OTFS, AFDM, OCDM) are inherently robust on doubly dispersive channels. | Shown for sparse channels of one to eight paths at SNR well above 0 dB, mostly with known or pilot-estimated channels. With overheads counted, OFDM and OTFS both come close to ideal on sparse channels [Gau22] [P, read]; uncoded OTFS has asymptotic diversity one [Sur19] [P, read]; the gain equals that of generic orthogonal precoding [Zem18] [G, preprint read]; chirp multiplexing with linear equalisers is worse than OFDM at low SNR [OZ16] [P, read]. |
| Chirp spread spectrum is immune to Doppler. | Immune to a static shift when the bandwidth is wide; satellite tests report link failure under rapid *change* of shift at the longest symbols [Dor19, Zad22] [P, abstracts]. Nothing read concerns spread. |
| The military robust waveforms are proven at negative SNR with 10 ms and 50 Hz. | The verified requirement is 2 dB in 3 kHz at 5 ms / 5 Hz [MIL110B]. Spreads above 10 ms and 50 Hz were *observed* on test links [JM99], which is not a requirement. Negative SNR in 3 kHz is still above about 23 dB-Hz. |
| Amateur modes show −30 dB is routine. | −30 dB in 2500 Hz is +4 dB-Hz. Modes that go below 0 dB-Hz need spread below a fraction of a hertz and minutes of transmission [FST4]. |
| An underspread channel can be tracked. | Identifiability is a noise-free statement about learning the channel [HB13, MBH13]. |
| Fades last a fraction of a second. | F.520-2's simulator range is 0.05–1.5 s; theory gives seconds at 0.1 Hz spread; LF and MF links show minutes [F520, P1321]. |

## 5. Gap analysis: literature against the system as built

| Topic | What the literature supports | As built | Gap |
| --- | --- | --- | --- |
| Meaning of the bound | Real conditions vary on several axes, with widths and shifts decorrelating in minutes (4.1). | One assumed second ray at one position, with a free gain (3.4). | The bound is a point in delay and offset, not a range; width and drift are not represented. |
| Channel used in tests | The ITU-R model is stationary, validated on 36 minutes of data; testing only on one's own model is optimistic (4.2). | Simulation applies the receiver's own two-ray model, Margin included (3.5). | No existing test can reveal a mismatch. In-house, the most direct mismatch screen failed 0 of 12 (3.8). |
| Meaning of the Hz value | In F.1487 it is the width of random fading per path, with no offset between paths. | A constant offset between two steady rays. | Same number, different channel: a steady beat instead of random fading. |
| The common shift itself | Tenths of a hertz to about a hertz on HF paths at lower frequencies (4.1). | Searched only within the oscillator bounds and the Doppler field; the spread setting does not widen it (3.2, 7.1). | In the GPS-disciplined rows the search is ±9 mHz or less, so a real shift lies outside it unless declared. |
| Drift of the common shift | Ordinary disturbances imply roughly 0.3–7 mHz/s; flares far more. Coherent tolerance falls as 1/T² (4.1, 4.3). | Whole-bit coherent proposal score; no drift hypothesis (3.2). | Tolerated at 11.6 s bits; not at bits of a minute or more, which include the hopping HF row at 257 s (7.1). |
| Random spread | A 0.1 Hz spread costs a whole-bit coherent score about 3 dB at 11.6 s and 10 dB at 70 s (4.3). | Coarse stage has no fading model; fine stage covers coherence of 0.5 s and longer (3.2, 3.3). | Cushioned by margin at the HF manual rows up to about a hertz or more; at low levels a true candidate may not be proposed; faster fading has no fine hypothesis. |
| Margin | No measurement supports 3× the worst published values (4.1). | Default 3×, applied to both delay and Doppler. | The assumed second ray moves further from anything observed. |
| Fading across the carriers | Usually flat across 225 Hz; selective at multi-millisecond delay (4.1). | One shared gain per ray; the selective setting is not modelled (3.4). | Correct in ordinary conditions, mismatched towards the bound. |
| Signal level | Swings of 10–20 dB in about 10 s are reported (4.1, grey). | Gain prior tied to the Target C/N0 (3.3). | Unknown sensitivity to level mismatch. |
| Added hypotheses | Averages are valid and cost at most the log of their number (4.4). | Admission is already an average; two extra lanes are computed but unused (3.3). | Low-cost headroom exists in evidence; the constraint is computing. |
| Carrying information between slots | Shift decorrelates in minutes (4.1). | Each slot searched independently, at least 60 s apart (3.1, 3.2). | Some correlation between neighbouring slots is unused. |
| Fundamental limits | Under random spread the time per bit grows as spread/(C/N0)² (4.3). | The planner counts spread only through an option the GUI never sets (3.4). | Planned durations assume a stable channel. Not binding at the HF manual rows' level. |
| Three carriers | Mutual reference works when the carriers share a gain and per-cell SNR is near 0 dB or better (4.5). | One coherent template (3.1, 3.2). | The mechanism closest to the stated purpose is unused. It is available at the HF manual rows' level and not at low levels. |
| Test length | F.1487 asks for 3000 fade periods (4.2). | Policy requires minimal, stratified tests. | Stationary long runs are neither affordable nor informative about variation; a stratified design is needed (7). |

## 6. Possible implementations

These are options, not prescriptions. None has been measured in the current native receiver. Each is described with its basis, expected benefit, cost, risks, and the cheapest test that would decide it. Better approaches than any listed here may exist; section 6.6 names some that were not evaluated.

### 6.0 Constraints every option must respect

- **Treat the bound as a range on several axes** (common shift and drift, inter-ray offset, per-ray spread, delay), not as one position.
- **Charge every added hypothesis** in the admission mixture, with weights fixed in advance, and keep charging the full public grid (section 4.4).
- **Do not let added flexibility absorb the keyed pattern.** The in-house wrong-key admissions (section 3.8) are the warning.
- **Fit the worst case inside the computing ceiling.** Automatic mode must prioritise challenging-case throughput, so an option that helps only on average does not raise the ceiling. Preserve every public hypothesis, quiet-slot completion, `W=2*H` and native C++ processing.
- **Find the failure before fixing it.** The project's operating guide says the historical evolving-ray and phase-particle likelihoods should be revisited "only when a measured channel failure identifies the missing dynamics" and the native alternative preserves the full search, the false-admission accounting and the end-of-message tests. The same order makes sense for every option here, and the simulation options below are what would produce that measurement.

### 6.1 Simulation side

| Option | Mechanism | Basis | Cannot represent | Cost and risk | Confidence |
| --- | --- | --- | --- | --- | --- |
| **S1. Separate the simulated channel from the receiver's bound** | Give the simulated channel its own severity per axis, including zero, instead of copying the receiver's Margin-scaled values. | F.520-2 sweeps spread, delay and level ratio independently of the modem [F520]; section 4.2 on testing a receiver on its own model. | Change within a run. | No run-time cost. The exporter tool already generates such channels; the work is routing them to the receiver under test. | High that it is needed. It is the precondition for every other test. |
| **S2. Second-ray level and specular-plus-diffuse mixes** | Vary the second ray from absent to equal power; mix a steady component with a fading one. | F.520-2 level ratios of −40 to 0 dB; specular returns by day and diffuse at night [Wag88, Wag95]; the DRM "Rice with delay" profile [DRM]. | More than two modes. | None at run time. | High. |
| **S3. Stationary random rays** | Enable the existing Gaussian-spectrum and Ornstein–Uhlenbeck ray gains at a chosen width. | F.1487 and Watterson for Gaussian; [VH92] for the Lorentzian alternative. | Non-stationarity. | Low. Risk: planning durations as for a stable channel. | High as a baseline; which spectrum shape is right depends on conditions. |
| **S4. A wandering common shift** | Apply a slowly varying frequency to the whole signal: a ramp, a slow sinusoid, a bounded random walk, optionally a flare-like pulse. | Magnitudes in section 4.1; a sinusoidal phase contamination is used as a synthetic model in the radar literature [Li14]; a triangular frequency sweep is a standard modem test [MIL110B]. | Coupling between shift and spread. | Negligible. Risk: the parameters are stress assumptions, since no measured drift rate was found. | Magnitudes medium; process law unsettled. |
| **S5. Severity that changes during a run** | First a scheduled ramp or step; later a slow bounded random process or switching between states (absent, specular, diffuse, attenuated). | Local stationarity and state-switching models [Mat05, Sad08]; a level model with 4 dB standard deviation and a 10 s time constant described in [Kil16] [G]. | Calibrated transition rates. | Negligible. Risk: reading stress settings as statistics. | Medium as a stress; low as statistics. |
| **S6. Replay of measured Doppler traces** | Drive the common shift from recorded standard-frequency Doppler data. | Open 1 s-cadence recordings [Col23]; replay is validated only for underwater channels [Otn13]. | Delay, spread, other regions and frequencies. Flare shifts were not proportional to frequency [Chu18]. | A lookup. Risk: generalising from North American mid-latitude paths at 2.5–10 MHz. | High for what it covers; low generality. |
| **S7. Selectivity from the delay geometry** | Test "the carriers stop sharing a gain" by varying the second-ray delay and level, alongside the synthetic per-branch modulation; use the existing bounded random law as well as the sinusoid. | Two-path correlation in section 4.1; the in-house finding that strong inter-carrier selectivity at short delay is a synthetic stress [H]. | — | None at run time. | Medium. |
| **S8. Band-specific schedules** | EME: spread following the libration rate. LF/MF: slow log-normal level and ground-plus-sky-wave interference in place of "Doppler". | [Tay10] [G]; [P1321]. | — | Low. Risk: thin sources. | Medium. |

Two decisions go with S3. First, whether a preset's Hz value should mean an inter-ray offset (as now) or a per-ray spread (as in F.1487); exposing both is safer than silently changing meaning. Second, the duration planner already has an option that counts spread (section 3.4); a random channel without it would be planned too short.

### 6.2 Receiver side, with the existing transmitted waveform

**R0. Keep the current design.**
- *Where it is near-optimal:* one stable ray; two steady rays at exactly the assumed position, at any relative strength; fading that stays coherent for the whole bit at the proposal stage and for half a second or more at the fine stage.
- *Predicted exposures [E], none measured natively:* a second ray at any other position (0 of 12 in the most direct in-house screen); diffuse spread and drift at the proposal stage, cushioned by margin at the HF manual rows and not at low levels (section 4.3); carriers that do not share a gain; a signal level far from the Target C/N0, to which the gain prior is tied.
- *Role:* the baseline every other option must beat in stage 1 of section 7.

**R1. Let a predeclared mixture of the two lanes decide.**
- *Mechanism:* the combined evidence, an equal-weight average of the fixed-affine and adaptive lanes, is already computed. Admit on it.
- *Basis:* averaging with predeclared weights is valid [VW21, Ram23]; a bank of models is the exact Bayes rule when the truth is one of them [Mag65] [P, abstract]. The historical isolated receiver admitted on this mixture [H]. The adaptive lane's moving clock rate is the one existing mechanism that resembles a drift allowance (section 3.2).
- *Benefit:* only where the adaptive lane beats the fixed lane by more than ln 2. Possibly none: in-house, a diffuse transition prior drained that lane [H].
- *Cost:* no computing. At most ln 2 nats, under 0.1 dB at this threshold [E].
- *Risk:* pruning without renormalising can only lower a lane's evidence, which keeps the guarantee, but this should be audited.
- *Test:* stage 1 already records all three evidences.
- *Confidence:* high on the accounting, low on the gain.

**R2. A severity ladder inside the fine mixture.**
- *Mechanism:* widen the set of channel hypotheses that are averaged: second tap absent, at the assumed position, at a small set of other positions, or supplied by R4; coherence times below 0.5 s, with correspondingly shorter blocks, when the configured bound calls for them; and values between 2 s and infinite, which is where quiet HF paths fall (0.03–0.1 Hz corresponds to roughly 5–15 s) [E].
- *Basis:* [Mag65]; per-block selection among Doppler subspaces at fractions of the maximum [ZMF06]; the Q65 ladder of spread hypotheses [Q65]; the loosely coherent kernel [Der10]. In-house, a receiver that averaged over a relative ray-frequency correction with half the prior at zero took a mismatch case from 4 of 8 to 8 of 8; a dynamic receiver with 25 ms blocks and a shorter coherence bank received 30 of 32 at 8 dB-Hz with a 5 Hz width, at known synchronisation; and one record notes that the default coherence alternatives do not cover a long but finite channel time scale [H].
- *Benefit:* fine evidence for channels inside the bound instead of at one position. It helps only candidates that reach the fine stage.
- *Cost:* about one more fine pass per added hypothesis over at most 12 coordinates. The fine stage's share of total runtime cannot be determined by reading. ln K nats of evidence.
- *Risks:* short blocks approach the noise-uncertainty floor at low C/N0 (section 4.3); more flexible gains can absorb the keyed pattern [H].
- *Test:* stage 1 with the true coordinate supplied directly.
- *Confidence:* medium.

**R3. Spread- and drift-tolerant proposal scoring.**
- *Mechanism:* before peak retention, also sum the existing coarse scores over runs of adjacent frequency points, on a short ladder of run lengths such as 1, 4, 16, and keep proposals from each. The fine stage still decides and the full grid is still charged.
- *Basis:* a drifting or spread signal's coherent power is spread over ḟ·T² or B·T frequency bins [Ran02]; summing power over them is the low-SNR form of the estimator–correlator [Kai60]; open-loop search with noncoherent sums of short coherent blocks is what worked for the Mars landing signal [Sat03].
- *Benefit [E]:* recovers much of the proposal-stage loss tabulated in section 4.3 for moderate drift and spread. Nothing on stable channels, where run length 1 is unchanged.
- *Cost:* the sums are cheap next to the transforms. More proposals mean more fine work, and every bulk backend's peak logic is affected. Not measured.
- *Risks:* oversampled bins are correlated; a proposal found this way helps only if the fine stage has a hypothesis that fits it, so R3 without R2 is a partial measure.
- *Test:* export coarse scores around the true coordinate in stage 1 cases d and e and smooth them offline. No production change is needed to decide.
- *Confidence:* medium-high on the mechanism, low on the cost.

**R4. A second ray found from the data.**
- *Mechanism:* score with one ray, look for a second peak within the configured delay and offset bound around each retained proposal, and pass the measured position to the fine stage as one more averaged hypothesis, keeping the tap at the assumed position as another.
- *Basis:* matched filtering on a delay–Doppler grid resolves paths [SA99] [P, read]; sparse channels with unknown support are identifiable in the noise-free case [HB13]. The in-house joint ray-frequency receiver is prior art [H].
- *Benefit [E]:* up to 3 dB when a second, equal, steady ray exists away from the assumed position. None for diffuse channels. One-ray scoring also removes the ray factor from coarse cost.
- *Cost:* possibly lower coarse cost in spread configurations, plus a peak search. Each offset cell must be charged prior mass.
- *Risks:* when the channel really is the assumed two equal rays, one-ray proposal scoring gives up as much as 3 dB at the proposal stage; sidelobes of the keyed pattern; weak second rays go unfound; real paths are not two clean rays.
- *Confidence:* medium-low.

**R5. A prior carried from slot to slot, as a weight and never as pruning.**
- *Mechanism:* after an admitted slot, put weight w on a neighbourhood of its frequency, arrival and clock coordinate and 1 − w on the full declared prior.
- *Basis:* priors that depend only on earlier data keep the guarantee [Ram23].
- *Benefit [E]:* a few nats of the grid correction on later slots, of the order of 1 dB. A wrong carry-over costs ln(1/(1 − w)).
- *Limits:* slots are at least 60 s apart, and on a quiet path the shift decorrelates in a few minutes [Jon86], so neighbouring slots are only partly correlated and the neighbourhood must allow for drift between them. It does nothing within a slot.
- *Constraint:* it ends today's per-slot independence, which is a policy decision.
- *Confidence:* high on the statistics, medium on the benefit.

**R6. Helper-informed weights, never gates.**
- *Mechanism:* the pilots are present for 38 s before DATA and through it. Use their measured offset and line shape to weight hypotheses about the current shift, drift and spread.
- *Basis:* in-house, the pilots improved the dynamic receiver at known synchronisation, and joint pilot scoring failed when timing was unresolved. The record leaves "frequency-only pilot assistance" as follow-up work, with the rule that pilot scores must not veto the DATA path [H].
- *Benefit:* unknown. At best it concentrates the prior on the right hypotheses, which is worth a fraction of a decibel in evidence and nothing in computing unless used for ordering.
- *Cost:* small; the Helper stage already fits pilot lines.
- *Risks:* the pilots are 1.85 kHz apart and outside the DATA band, so under multi-millisecond delay their fading differs from DATA's; Helper acquisition was unresolved under a 0.5 Hz ray width in the in-house record, and has several recorded fallback cases [H].
- *Test:* stage 1, recording what the Helper stage reports under spread and drift.
- *Confidence:* low until that is known.

**R7. A per-carrier-gain hypothesis.**
- *Mechanism:* one more averaged hypothesis in which the three carriers have separate gains per ray, for conditions where they no longer share one.
- *Basis:* the two-path correlation in section 4.1; in-band diversity in the standard parallel-tone modems [MIL110B].
- *Benefit:* only when the delay is several milliseconds. In-house, a shared-gain fit kept a median 71 % of the energy under the 65 % sinusoidal stress [H].
- *Cost [E]:* three times the dimensions; going from rank 2 to rank 6 costs about 1 dB, less as an averaged hypothesis.
- *Risks:* the keyed power permutation is itself a pattern across the carriers, so per-carrier gains must be held fixed over many cells or they will absorb it. A free sideband basis was rejected in-house for this kind of reason [H].
- *Confidence:* medium-low.

**R8. A block-noncoherent lane across the three carriers.** This is the option closest to the stated purpose of the three carriers (section 4.5).
- *Mechanism:* project each cell, or a short run of cells, of the three-carrier observation onto each bit's keyed pattern; sum the squared magnitudes over the bit, weighted for an assumed spread if cells are grouped. Use the result as a further proposal statistic, and possibly as one more averaged hypothesis.
- *Basis:* mutual reference between simultaneous components (section 4.5); spread-matched energy metrics in an operational decoder [Q65]; noncoherent tone detection after open-loop search [Sat03]; the weak-signal scaling of noncoherent sums [Low99].
- *Benefit [E]:* a frequency grid hundreds to thousands of times coarser, and no clock or drift hypothesis inside a block. With 60 Hz chips it gives up about 3.6 dB of evidence at 20 dB-Hz and under 1 dB above 28 dB-Hz, while immune to drift, to a common shift of several hertz and to spread up to a fraction of the chip rate. At 8 dB-Hz it is about 13 dB worse than coherent detection, and at 0 dB-Hz it is of no use. Section 7.1 gives the figure for each manual row: it would cost 0.03 dB in the low-rate rows and cannot be afforded in the EME row.
- *Conditions:* delay small enough that the carriers share a gain; noise level known closely, since this is an energy statistic (section 4.3). The *difference* between the two bit patterns' statistics has zero mean in noise whatever the noise level, which protects the bit choice but not the presence decision [E].
- *Cost:* not estimated with any confidence. The arrival search remains.
- *Risks:* weaker discrimination against a wrong key; a new statistic needs its own false-admission argument; in-house, products formed after projection "could not recover coherence already lost" [H].
- *Confidence:* high on the theory for its regime, medium on the magnitudes, low on the cost.

**R9. Feasibility reporting.**
- *Mechanism:* from the configured bound and the Target C/N0, compute the quantities of section 4.3 and tell the operator when the corner of the bound lies beyond what any receiver could do in the planned bit, instead of planning as for a stable channel. The planner's unused stochastic option is the natural place.
- *Basis:* [SH05]; the in-house information bounds [H].
- *Risk:* it depends on reading the preset's Hz value as a random spread. A steady two-ray reading has no such limit.
- *Confidence:* high for the bound, medium for the mapping.

**R10. Paying for added hypotheses.** These are trades, not gains.
- One-ray scoring at the proposal stage, with R4.
- Thinning the coarse frequency grid from 1/(4T) to 1/(2T) would halve the transform count while the worst-case loss rises from about 0.2 dB to about 0.9 dB [E]; interpolation between bins recovers most of such a loss [Ran02]. *Risk:* the grid is part of the public hypothesis set and of the planner's accounting, so this may conflict with the rule to preserve every public hypothesis and would need that question settled first.

**R11. Cover a declared common shift.** Section 7.1 shows the need: the GPS-disciplined rows search ±9 mHz or less.
- *Existing means:* the Doppler field widens the search by the magnitude of its shift.
- *Cost [E]:* frequency points grow as bound × bit length. For ±1 Hz that is about 30–50 times in rows 5 and 7 and about 100 times in row 6.
- *Cheaper routes, none measured:* a Helper, whose acquisition returns at most 64 narrowed intervals (section 3.2), within the limits noted under R6; a per-cell statistic (R8), which needs no frequency hypothesis for shifts of a few hertz where its conditions hold; a slot-to-slot prior (R5) once one slot has been found.
- *Risk:* a shift bound is one more operator assumption. The Doppler field is expressed as platform velocity, so using it for an ionospheric shift would need to be made explicit.
- *Test:* stage 1 case h.
- *Confidence:* high that the code behaves this way; the size of real shifts on a given path is the uncertain part.

### 6.3 Options that would need a protocol change

These are listed for completeness and were not examined in depth. A protocol change is a separate decision, made in the canonical source.

| Option | Basis | Expected effect [E] | Cost and risk |
| --- | --- | --- | --- |
| **On–off or one-hot keying across the three carriers** | Low-SNR theory favours on–off, tone-like signalling under a peak limit [SH05, Set09, TT00]. | About 9 dB more evidence from the power pattern than the 0.4 / 1 / 1.6 permutation; one carrier at a time also removes the peak-power penalty. | Changes the envelope and the noise-like appearance; a third of the cell-carriers carry no power. |
| **Longer symbols with tone spacing at least the expected spread, and coding across symbols** | The approach of the weak-signal modes in section 4.6. | Several decibels of energy per noncoherent symbol; an error-detecting check can then validate decodes; far less energy per bit. | A different protocol, with different acquisition and completion rules. |
| **Rate chosen from the bound** so that the cell is short against the coherence time and the triad narrow against the coherence bandwidth | Grid matching [JW07]. | Possible for the benign and EME bounds; not for HF moderate, HF worst or Aurora, where delay × Doppler is too large. | Rate becomes tied to the channel setting. |
| **Carriers far enough apart for real diversity** | Diversity needs independent branches [Sta85, TT00]. | Helps slow, short-delay fading when each branch has several decibels of SNR; harmful otherwise. | Hundreds of hertz to kilohertz of bandwidth; loses mutual reference. |
| **A bit-independent component** | Pilot components in satellite navigation [BL08]. | Decision-free tracking between slots; halves the two-pattern bank at most. | Its share of the power; training is inefficient at low SNR [HH03]. |

### 6.4 GUI and contract wording

- **What the settings do in live operation.** They are receiver assumptions; nothing synthetic is added to a real transmission. The code does this for every live Robust transmission, and the working-tree GUI contract now says so (section 3.6). Nothing further is needed beyond keeping that text.
- **Reconcile Margin.** The contract calls it a receiver search multiplier; in Robust simulation it also scales the simulated channel. With S1 the two would separate naturally.
- **Name what the Hz value means.** An inter-ray offset and a random spread are different channels with the same number (section 4.1).
- **If S1 is adopted,** the operator needs two things where there is now one: the bound the receiver assumes, and the channel the simulation applies.
- **Say what widens the frequency search.** The spread preset does not. The oscillator settings, Margin and the Doppler field do (section 7.1). An operator who selects "HF worst" on a GPS-disciplined link may expect otherwise.
- **Report which hypothesis carried the evidence.** The lane evidences are already shown as diagnostics. Extending that to any added hypotheses would tell an operator whether the configured bound is far from the conditions actually met.

### 6.5 Options judged unlikely to work here

- **Closed-loop phase or frequency tracking of DATA at low levels.** Demonstrated thresholds are 15–28 dB-Hz [Sat03, Hum05, Lop10]: 7–34 dB above the 8, 0 and −6 dB-Hz points. At the roughly 20 dB-Hz of the HF manual rows such loops are marginal, and deep fades defeat them at any level.
- **Coherent drift or second-derivative banks over the full grid.** The number of hypotheses grows as T² and T³ for fixed physical ranges [E, from the definitions in AR18]. For bits of a minute or more, covering ordinary drift at one-bin spacing would need tens of drift hypotheses, each multiplying coarse cost [E].
- **A maximum over frequency paths across all coarse cells as the admission statistic.** Its noise-only distribution is an extreme-value problem [JK02], which conflicts with the false-admission accounting. A sum over paths on the few fine candidates would be admissible.
- **Estimating the wander from the signal itself, as over-the-horizon radar does, at low levels.** That needs a reference that is strong within the wander's own time scale [Li14].
- **One detector matched to the whole bound.** Large loss on benign channels (section 4.4).
- **Free per-sideband or per-block gain bases.** Examined in-house and rejected as defaults, for the rank penalty and because they absorbed the keyed pattern [H].
- **Particle or evolving-ray likelihoods over the whole grid.** Far beyond the computing budget, and deferred by project guidance until a measured failure calls for them.

### 6.6 Not evaluated here, and possibly better

- Exact innovations (Kalman) likelihoods evaluated on a GPU across the coarse grid, not only on fine candidates.
- Sparse-Bayesian delay–Doppler estimation adapted to very low SNR. Published work uses strong pilots [Wei21].
- Learned detectors, which approach matched-filter sensitivity in restricted settings [DP20, JP23].
- A different waveform (section 6.3).
- Measured channel data for the intended paths. Almost nothing located measures the stationarity time or drift rate of HF Doppler, so a modest measurement campaign could narrow the bound more than any algorithm.

This list is certainly incomplete.

## 7. A staged, computationally minimal way to find out

This follows the project's rule for intensive Robust tests: cheap deterministic checks first; predeclared strata, sample budget, uncertainty decision and stopping rule; no long or exhaustive campaigns. Communication-success trials stay separate from capacity measurements.

**Stage 0: desk check, no code.** For each manual row, find the planned bit length and the simulated link's C/N0, then compute the quantities that decide exposure: energy margin, drift tolerance, independent cells per bit, energy per coherence time, delay × carrier spacing. This has been carried out for the eight manual rows; section 7.1 gives the tables. In short: the GPS-disciplined rows search a common shift of ±9 mHz or less; the hopping HF row is coherent over 257 s; the EME row has 6.5 dB of margin; row 1's link is 68 dB below its planning target; and HF rows 4, 5 and 7 have 14–15 dB of margin and tolerate ordinary drift.

**Stage 1: deterministic corner cases** (needs S1, and minimal forms of S3 and S4). The configuration's own bit length, C/N0 high enough that the matched case passes with margin, receiver at the bound throughout.

| Case | Channel | What it isolates |
| --- | --- | --- |
| a | none | cost of assuming a second ray that is not there |
| b | second ray at half the delay and half the offset | position mismatch inside the bound |
| c | second ray at 0, −10 and −20 dB | ray-level variation (expected to pass) |
| d | single ray with linear drift at 0.5, 2 and 5 × 1/T² | coarse coherence under drift |
| e | random width 0.03, 0.1, 0.5 and 2 Hz | coarse coherence under spread, and the fine stage beyond its shortest coherence time |
| f | worst-phase notch on each carrier in turn | shared-gain assumption |
| g | step from none to bound mid-bit and at a slot boundary | non-stationarity |
| h | constant common shift of 0.05, 0.2 and 1 Hz with the Doppler field at zero, on a GPS-disciplined row | whether an unsearched shift is tolerated (section 7.1) |

For each slot record three things separately, because they fail for different reasons: whether the true coordinate was among the coarse proposals; the fine evidence at the true coordinate; and all three lane evidences. The probe tool has a non-admitting fine-run kind; whether it accepts a supplied coordinate was not checked. A few dozen slots in total suffice. Decide per case, not on a pooled rate. Then repeat the cases that pass at a C/N0 chosen to remove the margin.

**Stage 2: stratified trials at operating C/N0**, only for cases stage 1 leaves open.

- Strata by the dimensionless products, not by preset names: spread × bit length (much less than, about, much more than one), drift × bit length², delay × spacing (0, 0.25, 0.5), ray level.
- A sequential test per stratum with a cap and an explicit "undecided" outcome, in place of a fixed large sample. Use common random numbers across strata.
- When spread × bit length is much less than one the channel is nearly frozen within a bit: estimate the error rate on a small grid of channel states and integrate over the amplitude law, which replaces the F.1487 run lengths.
- For any statement of the form "for all severities up to the bound", report the worst stratum, not an average.

**Stage 3: one severity sweep.** Ramp the severity from none to the bound and back on a known schedule within one run and regress slot outcome on the instantaneous severity. About a hundred slots give the response curve and expose any non-monotonic region.

**What short runs cannot establish.** Error probabilities below about three divided by the number of trials; long-run rates in slow fading; behaviour at real transitions or rare events; the false-admission rate at 10⁻⁴ per day; anything about field performance. Passing at "none" and at the bound does not imply passing in between.

**Evidence handling.** Keep raw outputs in ignored `local-evidence/` with a stated purpose and cleanup trigger, and distil findings, including negative ones, into the maintained guides.

### 7.1 Stage 0 carried out: the eight manual rows

**What was done.** Each row's planned configuration was read from the plan receipts that other work recorded on 2026-11-12 with the native probe's `plan` command (`local-evidence/rate-spread-burst/receipts/plan-r1-base.json` to `plan-r8-base.json`). Nothing was run for this document. The planner sources are unchanged between the commit those receipts were configured at (`98b82ccc`) and the tree read here, and the rows in `_tests-RobustModem.md` match the receipts' arguments. Bit lengths, windows, link levels and search bounds below come from the receipts. Everything else is a closed-form estimate [E] from the formulas of section 4.3, computed in one pass by a script that was not added to the repository. The receipts are temporary material of that other work and may be removed, so the values needed are copied here.

**How to read the estimates.**

- *Simulated level* is transmit power less path loss less the default noise density of −164 dBm/Hz. The Target C/N0 is 32 dB-Hz in every row.
- *Margin* is the bit's mean evidence on a stable channel that matches the receiver's assumption, by the planner's own mean formula (`src/triad_duration.cpp:118-120`), divided by what admission needs for that row's grid with one fine candidate. That need is 38 to 46 nats depending on the row.
- *1 dB* and *3 dB* are where the whole-bit coherent proposal score starts to lose.
- *Margin spent* is where the score's average energy has fallen to what admission needs. It is an optimistic limit. Under diffuse fading the coherent score is exponentially distributed, so 90 % detection needs roughly 10 dB more than the average suggests [E]. None of these figures says what the fine stage then does.

**Plan and link level.**

| Row | Configuration | Bit (one tile) | Part of the emitted bit that depends on the bit [E] | Window / slot spacing | Simulated level | Margin at simulated level / at Target [E] |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | Sub-9 kHz: 1 mW, 200 dB, Rate 0.1 Hz | 5,760.4 s (96 min) | 67 % | 6,767 s / 6,750 s | −36 dB-Hz | **−20 dB** / +54 dB |
| 2 | Groundwave 500 kHz, GPSDO: 4 W, 180 dB, Rate 1 Hz | 576.4 s | 67 % | 690 s / 690 s | 20 dB-Hz | +32 dB / +44 dB |
| 3 | Groundwave 500 kHz, Helper 40 W, free-running oscillators | 576.4 s | 67 % | 735 s / 720 s | 20 dB-Hz | +31 dB / +43 dB |
| 4 | HF 30 MHz, Helper 40 W, free-running oscillators, Rate 36 Hz, Spreading 10× | 11.6 s | 95 % | 71.5 s / 90 s | 20 dB-Hz | +14 dB / +26 dB |
| 5, 7 | HF 30 MHz, GPSDO, same waveform | 11.6 s | 95 % | 26.5 s / 60 s | 20 dB-Hz | +15 dB / +27 dB |
| 6 | HF 30 MHz, GPSDO, hopping | 256.8 s (642 dwells) | 6 % | 271.7 s / 270 s | 20 dB-Hz | +27 dB / +39 dB |
| 8 | EME 5.8 GHz: 40 W, 200 dB, Helper 15 kW, Rate 360 Hz, hopping | 26.4 s (66 dwells) | 6 % | 86.3 s / 90 s | 10 dB-Hz | **+6.5 dB** / +29 dB |

- **Every bit is exactly one tile, and the tile is set by geometry, not by the link.** A tile must hold 64 inner coefficients clear of its edges (`src/triad_modem_v3.cpp:144-187`). At Rate 1 Hz a cell lasts 6 s, so the tile is 576 s. With hopping only one cell in each 0.4 s dwell is protected, so 640 payload cells need 642 dwells. The 32 dB-Hz Target is met by the minimum tile in every row, so it shortens nothing; lowering it would lengthen bits in whole tiles.
- **In the hopping rows most of the emission is cover.** The transmitter is on for 72.5 % of each dwell (`src/robust_profile.cpp:218`, `src/triad_stream.cpp:83-84`), and all but one of the roughly 17 cells sent per dwell carry the cover pattern that is the same for both bits. The whole bit supports admission, but only 10.7 s of row 6 and 1.07 s of row 8 tell the two bits apart. At the simulated level those payload cells carry an E/N0 of about 30 dB in row 6 and about 10 dB in row 8.
- Shares are by cell count and on-time, not measured energy.

**Frequency search and what the proposal score tolerates [E].** The first two columns are from the receipts.

| Row | Common shift searched | Grid step; points | Shift beyond the grid: 1 dB / margin spent | Drift: 1 dB / margin spent | Diffuse spread: 3 dB / margin spent |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | ±0.17 µHz | 37 µHz; 2 | 39 µHz / no margin | 45 nHz/s / no margin | 0.19 mHz / no margin |
| 2 | ±50 µHz | 0.37 mHz; 2 | 0.39 mHz / 1.5 mHz | 4.5 µHz/s / 3.4 mHz/s | 1.9 mHz / 1.8 Hz |
| 3 | ±0.67 Hz | 0.37 mHz; 3,619 | 0.39 mHz / 1.5 mHz | 4.5 µHz/s / 2.9 mHz/s | 1.9 mHz / 1.5 Hz |
| 4 | ±90.5 Hz | 21 mHz; 8,596 | 22 mHz / 70 mHz | 14 mHz/s / 0.20 Hz/s | 0.11 Hz / 1.7 Hz |
| 5, 7 | ±9 mHz | 21 mHz; 2 | 22 mHz / 71 mHz | 14 mHz/s / 0.22 Hz/s | 0.11 Hz / 2.0 Hz |
| 6 | ±9 mHz | 0.97 mHz; 20 | 1.0 mHz / 3.7 mHz | 31 µHz/s / 7.2 mHz/s | 5 mHz / 1.5 Hz |
| 8 | ±0.58 Hz | 9.4 mHz; 125 | 9.8 mHz / 23 mHz | 2.9 mHz/s / 7.1 mHz/s | 48 mHz / 0.12 Hz |

Durations here are the rendered bit including pulse halos, which is 672 s for rows 2 and 3 and 6,720 s for row 1. Beyond the "margin spent" shift only the sidelobes of the coherent response remain, with nulls between them.

**Cell-level figures [E].**

| Row | Cell; carrier spacing | SNR in one cell at simulated level | Per-cell statistic against the coherent score | Assumed second ray: delay in cells; offset in spacings | Adjacent-carrier gain correlation at preset / assumed delay | Selective fast cycles per cell (depth) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 60 s; 0.021 Hz | −18 dB | −21 dB | about 0; 0.05 | 1.00 / 1.00 | off |
| 2, 3 | 6 s; 0.21 Hz | +28 dB | −0.03 dB | about 0; 0.48 | 1.00 / 1.00 | 0.6 (1 %) |
| 4–7 | 16.7 ms; 75 Hz | +2 dB | −3.6 dB | 1.26; 1.2 | 0.08 / 0.23 | 1.5 (65 %) |
| 8 | 16.7 ms; 75 Hz | −8 dB | −11 dB | 0.03; 0.08 | 0.99 / 0.99 | 0.1 (65 %) |

The fourth column is the mean evidence a cell-by-cell noncoherent statistic would collect, relative to the coherent score, if the three carriers share a gain (section 4.5, mechanism 1).

**What the desk check shows**, in order of how directly each point bears on reception under real conditions.

1. **The spread setting does not widen the common-frequency search.**
   - The searched range is the oscillator bounds times Margin plus the magnitude declared in the Doppler field (`src/robust_profile.cpp:240-244`). The Doppler/delay-spread preset moves only the assumed second ray.
   - In the GPS-disciplined rows the search is therefore ±9 mHz (rows 5–7), ±50 µHz (row 2) and ±0.17 µHz (row 1). Margin stretches the reach to about ±0.08 Hz in rows 5 and 7 and about ±13 mHz in row 6.
   - Shifts in the sources are tenths of a hertz to about a hertz at lower HF frequencies (section 4.1), and these rows are at 30 MHz. On these estimates a shift of a few tenths of a hertz would defeat rows 5–7 by itself, whatever spread is configured, unless it is declared.
   - Declaring it has a price. The Doppler field takes a radial velocity or a percentage (`src/tuning.cpp:36-84`); ±1 Hz at 30 MHz is about 36 km/h. It would multiply the frequency points by about 30–50 in rows 5 and 7 and by about 100 in row 6. In simulation the same field also applies that offset to the signal.
   - Row 4 searches ±90.5 Hz because of its free-running oscillators and is not exposed in this way. Rows 3 and 8 search ±0.67 Hz and ±0.58 Hz.
2. **Row 6, the hopping HF row, has the least room among the HF rows.** Its bit is coherent over 257 s. The proposal score loses 1 dB at a drift of 31 µHz/s and 3 dB at a diffuse spread of 5 mHz. Its 27 dB of margin is spent at about 7 mHz/s, which is the top of the range estimated for ordinary travelling disturbances (section 4.1), or at 1.5 Hz of spread. A spread of 0.03–0.1 Hz, the quiet-day range reported by two-way radar, would cost it 10–15 dB. Rows 4, 5 and 7 lose under 3 dB to the same conditions.
3. **Row 8, EME, has the thinnest margin and a spread that is random by nature.**
   - Its margin is 6.5 dB at the simulated level, and it is spent at a diffuse spread of 0.12 Hz.
   - The preset says 6 Hz. A lunar echo's spread comes from scattering over a librating surface, so it is a random width and not a steady second ray; one computed median for 5.76 GHz is 25 Hz at half power [Tay10].
   - The payload cells are 0.4 s apart, much longer than the roughly 75 ms coherence time of a 6 Hz spread, and each has −8 dB of SNR.
   - The capacity bound of section 4.3 would allow a bit in about 0.24 s at this level and spread, before detection overheads. So what limits the row on these estimates is the coherent proposal score and the small payload share, not the channel's information limit.
   - The geometric Doppler at 5.8 GHz is of the order of ±18 kHz and changes by up to about 1.3 Hz/s [E]. The row needs it removed to within ±0.6 Hz and a few millihertz per second.
   - In the 12 November screening this row missed DATA when its Rate was raised ([validation](../../../docs/validation.md), "Manual rate, spreading and burst trends") [H], which is consistent with a thin margin.
4. **Row 1's plan and link differ by 68 dB.** At 1 mW and 200 dB the simulated level is −36 dB-Hz. One planned tile then carries about 1.4 nats of energy against about 38 needed. The duration comes from the default Target. As a computing-capacity geometry that may be intended. As a link it would need roughly thirty times the energy, about two days per bit at this Rate.
5. **Rows 2 and 3 have long bits and large margins.** A 576 s coherent bit loses 1 dB at 4.5 µHz/s and 3 dB at 1.9 mHz of diffuse spread; the 31–32 dB of margin is spent at about 3 mHz/s or 1.5–1.8 Hz. These are ground-wave paths, where the sources expect little shift. Two things could still matter. Row 2 reaches only ±1.5 mHz, so a night-time sky-wave component with excursions near 0.1 Hz [Tro24] would lie outside it if it dominated. Row 3's free-running oscillators are a more likely source of drift than the channel, and were not assessed here.
6. **Rows 4, 5 and 7 are cushioned against drift and exposed mainly through geometry.** With 14–15 dB of margin the proposal score keeps enough average energy up to about 0.2 Hz/s of drift and about 2 Hz of diffuse spread. At the configured 3× Margin the assumed second ray lies 1.26 cells later and 1.2 carrier spacings higher than the first, and adjacent carriers' gains would be only 0.23 correlated at that delay.
7. **Where a per-cell three-carrier statistic would pay** (mechanism 1 of section 4.5; option R8).
   - Rows 2 and 3 have +28 dB in each cell. The statistic would give up 0.03 dB and need the frequency only to a fraction of 0.17 Hz, so row 3's 3,619 frequency points could become a few tens.
   - The HF rows would give up 3.6 dB of their 14–15. In return a common shift of a few hertz would need no frequency hypothesis at all, against 30–100 times more points for the coherent grid (item 1).
   - Row 8 would lose 11 dB with 6.5 dB in hand, and row 1 has no margin, so neither can use it.
   - The condition that the carriers share a gain holds at the configured delays of rows 1–3 and 8 and fails at those of rows 4–7.

**What this adds to stage 1.** A case with a constant common shift on a GPS-disciplined row (case h); row 6 as the first HF row to test for drift and spread; and row 8 with a random width in place of the steady second ray.

**Limits of this check.**

- These are mean-energy estimates for the proposal stage on an otherwise stable channel. They are not measurements, and they say nothing about the fine stage or about false admissions.
- The receipts were read, not regenerated. The simulated level assumes the default noise density, which an operator can change.
- Hopping-row energy uses the envelope's on-time and the cell counts.
- The literature magnitudes used for comparison carry the cautions of section 4.1, and none of them is for 30 MHz or 500 kHz paths specifically.

## 8. References

Status: **read** = the supporting passage was read in the source or an author's or repository copy; **read (extraction)** = read through a text-extraction tool, not by eye; **abstract** = abstract or index record only; **grey** marks non-refereed material. Preprint versions were read where noted; they were not compared line by line with the journal versions.

### Standards and technical reports

| Key | Source | Status |
| --- | --- | --- |
| F1487 | Rec. ITU-R F.1487 (2000), Testing of HF modems with bandwidths of up to about 12 kHz using ionospheric channel simulators. [itu.int](https://www.itu.int/rec/R-REC-F.1487) | read |
| F520 | CCIR Rec. 520-2 (1992, withdrawn), Use of high frequency ionospheric channel simulators. [itu.int](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.520-2-199203-W!!PDF-E.pdf) | read |
| DRM | ETSI ES 201 980 V4.2.1 (2021), Digital Radio Mondiale system specification, Annex B. | read |
| P1321 | Rec. ITU-R P.1321-5 (2015), Propagation factors affecting systems using digital modulation techniques at LF and MF. | read |
| MIL110B | MIL-STD-188-110B (2000), Interoperability and performance standards for data modems: §5.3.2.5 and Table XX; Appendices A and B; Appendix C §C.6.3–C.6.4. | read |
| NOAA | NOAA Space Weather Prediction Center, NOAA Space Weather Scales. [spaceweather.gov](https://www.spaceweather.gov/noaa-scales-explanation) | read (extraction); grey |
| VH90 | Vogler, Hoffmeyer (1990), A new approach to HF channel modeling and simulation, Part II, NTIA Report 90-255. | read |
| VH92 | Vogler, Hoffmeyer (1992), same series, Part III, NTIA Report 92-284. | read |
| Aut62 | Auterman (1962), Fading correlation bandwidth and short-term frequency stability measurements on a high-frequency transauroral path, NBS Technical Note 165. | read |
| Sat03 | Satorius, Estabrook, Wilson, Fort (2003), Direct-to-Earth communications and signal processing for Mars Exploration Rover entry, descent, and landing, JPL IPN Progress Report 42-153. | read |
| Low99 | Lowe (1999), Voltage signal-to-noise ratio nonlinearity resulting from incoherent summations, JPL TMO Progress Report 42-137. | read |
| Fit93 | Fitzgerald, Argo, Carlos (1993), Measurements of spatial and frequency coherence of an equatorial HF path during spread-F, Los Alamos LA-UR-93-2140. | read; poor scan |
| TM04 | Tufvesson, Molisch (2004), Ultra-wideband communication using hybrid matched filter correlation receivers, MERL TR2004-016. | read |
| JM99 | Jorgenson, Moreland (1999), HF serial-tone waveform design, NATO RTO MP-26, paper 33. | read |

### Channel measurements and physics

| Key | Source | Status |
| --- | --- | --- |
| Jon86 | Jones, Georges, Riley (1986), Measured ionospheric distortion of HF ground-backscatter spectra, IEEE Trans. Antennas Propag. 34(4):563–568, doi:10.1109/TAP.1986.1143842 | read |
| Geo80 | Georges (1980), Progress toward a practical skywave sea-state radar, IEEE Trans. Antennas Propag. 28(6):751–761, doi:10.1109/TAP.1980.1142429 | read; poor scan |
| Sto11 | Stocker, Warrington (2011), Radio Science 46, RS1014, doi:10.1029/2010RS004482 | read |
| Sto13 | Stocker, Warrington, Siddle (2013), Radio Science 48:638–645, doi:10.1002/2013RS005264 | read |
| War06 | Warrington, Stocker, Siddle (2006), Radio Science 41, RS2006, doi:10.1029/2005RS003294 | read |
| Ang98 | Angling, Cannon, Davies, Willink, Jodalen, Lundborg (1998), Radio Science 33(1):97–107, doi:10.1029/97RS02206 | abstract |
| Jod01 | Jodalen, Bergsvik, Cannon, Arthur (2001), Radio Science 36(6):1687–1698, doi:10.1029/2000RS002547 | abstract |
| Wag88 | Wagner, Goldstein, Meyers (1988), Radio Science 23(4):555–568, doi:10.1029/RS023i004p00555 | abstract |
| Wag95 | Wagner, Goldstein, Rupar, Kennedy (1995), Radio Science 30(3):659–676, doi:10.1029/94RS03177 | abstract |
| Mal21 | Male, Porte, Gonzalez, Maso, Pijoan, Badia (2021), Sensors 21(6):2210, doi:10.3390/s21062210 | read (extraction) |
| Chu18 | Chum et al. (2018), Continuous Doppler sounding of the ionosphere during solar flares, Earth Planets Space 70:198, doi:10.1186/s40623-018-0976-4 | read |
| Cha22 | Chakraborty et al. (2022), J. Geophys. Res. Space Physics 127(6), e2022JA030342, doi:10.1029/2022JA030342 | read (extraction) |
| Aks24 | Aksonova et al. (2024), Adv. Space Res. 73(9):4414–4425, doi:10.1016/j.asr.2024.01.032 | abstract |
| Fri22 | Frissell et al. (2022), Geophys. Res. Lett. 49(5), e2022GL097879, doi:10.1029/2022GL097879 | abstract |
| Col23 | Collins et al. (2023), Crowdsourced Doppler measurements of time standard stations demonstrating ionospheric variability, Earth Syst. Sci. Data 15:1403–1418, doi:10.5194/essd-15-1403-2023 | read (extraction) |
| Tro24 | Trop et al., Tracking traveling ionospheric disturbances through Doppler-shifted AM radio transmissions, EGUsphere preprint egusphere-2024-2383 | read; grey (preprint) |
| Tho25 | Thomson, Clilverd, Rodger (2025), J. Geophys. Res. Space Physics 130(4), e2024JA033610, doi:10.1029/2024JA033610 | read in part |
| Suk96 | Sukhorukov (1996), Annales Geophysicae 14(1):33–41, doi:10.1007/s00585-996-0033-7 | abstract |
| Wan19 | Wang et al. (2019), Earth Planets Space 71:47, doi:10.1186/s40623-019-1026-6 | read in part |
| Tay10 | Taylor (2010), Frequency-dependent characteristics of the EME path. [wsjt.sourceforge.io](https://wsjt.sourceforge.io/EME2010_K1JT.pdf) | read; grey |
| Kil16 | Kille (Isode Ltd, 2016), Optimizing applications and data links for HF radio intermediate term variation, Nordic HF conference. | read; grey |
| Fur09 | Furman, Koski (2009), Standardization of an intermediate duration HF channel variation model, IET IRST 2009, doi:10.1049/cp.2009.0051 | abstract; grey |
| Tur25 | Turbic, Stańczak (2025), arXiv:2506.05898, restating Jakes (1974) for level crossings and fade durations. | read (extraction); grey (preprint) |

### Non-stationary channels and replay

| Key | Source | Status |
| --- | --- | --- |
| Mat05 | Matz (2005), On non-WSSUS wireless fading channels, IEEE Trans. Wireless Commun. 4(5):2465–2478, doi:10.1109/TWC.2005.853905 | abstract |
| Ber14 | Bernadó, Zemen, Tufvesson, Molisch, Mecklenbräuker (2014), IEEE Trans. Veh. Technol. 63(1):82–93, arXiv:1305.3376 | abstract |
| Soc11 | Socheleau, Laot, Passerieux (2011), IEEE Trans. Signal Process. 59(10):4838–4849, doi:10.1109/TSP.2011.2160057 | abstract |
| Wal17 | van Walree, Socheleau, Otnes, Jenserud (2017), IEEE J. Oceanic Eng. 42(4):1007–1018, doi:10.1109/JOE.2017.2699078 | abstract |
| Otn13 | Otnes, van Walree, Jenserud (2013), IEEE J. Oceanic Eng. 38(4):689–700, doi:10.1109/JOE.2013.2262743 | abstract |
| Sad08 | Sadeghi, Kennedy, Rapajic, Shams (2008), IEEE Signal Process. Mag. 25(5):57–80, doi:10.1109/MSP.2008.926683 | record only |
| — | Kaipio, Somersalo (2007), Statistical inverse problems: discretization, model reduction and inverse crimes, J. Comput. Appl. Math. 198(2):493–504 | title only |

### Limits

| Key | Source | Status |
| --- | --- | --- |
| SH05 | Sethuraman, Hajek (2005), Capacity per unit energy of fading channels with a peak constraint, IEEE Trans. Inf. Theory 51(9):3102–3120, doi:10.1109/TIT.2005.853329 | read |
| Set09 | Sethuraman, Wang, Hajek, Lapidoth (2009), Low-SNR capacity of noncoherent fading channels, IEEE Trans. Inf. Theory 55(4):1555–1574, arXiv:0712.2872 | read (preprint) |
| Dur10 | Durisi, Schuster, Bölcskei, Shamai (2010), Noncoherent capacity of underspread fading channels, IEEE Trans. Inf. Theory 56(1):367–395, arXiv:0804.1748 | read (preprint) |
| TT00 | Telatar, Tse (2000), Capacity and mutual information of wideband multipath fading channels, IEEE Trans. Inf. Theory 46(4):1384–1400, doi:10.1109/18.850678 | read |
| Ver02 | Verdú (2002), Spectral efficiency in the wideband regime, IEEE Trans. Inf. Theory 48(6):1319–1343, doi:10.1109/TIT.2002.1003824 | read |
| ZTM07 | Zheng, Tse, Médard (2007), Channel coherence in the low-SNR regime, IEEE Trans. Inf. Theory 53(3):976–997, doi:10.1109/TIT.2006.890777 | read (preprint) |
| ZT02 | Zheng, Tse (2002), Communication on the Grassmann manifold, IEEE Trans. Inf. Theory 48(2):359–383, doi:10.1109/18.978730 | read |
| HH03 | Hassibi, Hochwald (2003), How much training is needed in multiple-antenna wireless links?, IEEE Trans. Inf. Theory 49(4):951–963, doi:10.1109/TIT.2003.809594 | read |
| Ost19 | Östman, Durisi, Ström, Coşkun, Liva (2019), IEEE Trans. Commun. 67(2):1521–1536, arXiv:1712.06387 | read (preprint) |
| Sta85 | Stark (1985), Capacity and cutoff rate of noncoherent FSK with nonselective Rician fading, IEEE Trans. Commun. 33(11):1153–1159 | read |
| StaN | Stark, EECS 555 course notes, University of Michigan, Chapter 11: Fading channels | read; grey |
| AF01 | Abou-Faycal, Trott, Shamai (2001), IEEE Trans. Inf. Theory 47(4):1290–1301, doi:10.1109/18.923716 | abstract |
| Kai62 | Kailath (1962), Measurements on time-variant communication channels, IRE Trans. Inf. Theory 8(5):229–236, doi:10.1109/TIT.1962.1057748 | abstract |
| Bel69 | Bello (1969), Measurement of random time-variant linear channels, IEEE Trans. Inf. Theory 15(4):469–475, doi:10.1109/TIT.1969.1054332 | abstract |
| PW06 | Pfander, Walnut (2006), Measurement of time-variant linear channels, IEEE Trans. Inf. Theory 52(11):4808–4820, doi:10.1109/TIT.2006.883553 | abstract |
| HB13 | Heckel, Bölcskei (2013), Identification of sparse linear operators, IEEE Trans. Inf. Theory 59(12):7985–8000, arXiv:1209.5187 | read (preprint) |
| MBH13 | Matz, Bölcskei, Hlawatsch (2013), Time-frequency foundations of communications, IEEE Signal Process. Mag. 30(6):87–96, arXiv:1307.4790 | read (preprint) |
| JW07 | Jung, Wunder (2007), The WSSUS pulse design problem in multicarrier transmission, IEEE Trans. Commun. 55, arXiv:cs/0509079 | read (preprint) |
| TS08 | Tandra, Sahai (2008), SNR walls for signal detection, IEEE J. Sel. Topics Signal Process. 2(1):4–17, doi:10.1109/JSTSP.2007.914879; formulas from Tandra (2009), PhD thesis, UC Berkeley EECS-2009-192 | abstract; thesis read |

### Detection, search and tracking

| Key | Source | Status |
| --- | --- | --- |
| SF94 | Scharf, Friedlander (1994), Matched subspace detectors, IEEE Trans. Signal Process. 42(8):2146–2157, doi:10.1109/78.301849 | read |
| Kai60 | Kailath (1960), Correlation detection of signals perturbed by a random channel, IRE Trans. Inf. Theory 6(3):361–366, doi:10.1109/TIT.1960.1057563 | abstract |
| SA99 | Sayeed, Aazhang (1999), Joint multipath-Doppler diversity in mobile wireless communications, IEEE Trans. Commun. 47(1):123–132, doi:10.1109/26.747819 | read |
| Der10 | Dergachev (2010), On blind searches for noise dominated signals: a loosely coherent approach, Class. Quantum Grav. 27, 205017, arXiv:1003.2178 | read (preprint) |
| ZHS05 | Zemen, Hofstetter, Steinböck (2005), Successive Slepian subspace projection in time and frequency for time-variant channel estimation, IST Mobile Summit. Journal companion: Zemen, Mecklenbräuker (2005), IEEE Trans. Signal Process. 53(9):3597–3607 (abstract) | read |
| ZMF06 | Zemen, Mecklenbräuker, Fleury (2006), Minimum-energy bandlimited time-variant channel prediction with dynamic subspace selection, EUSIPCO 2006 | read |
| Li98 | Li, Cimini, Sollenberger (1998), Robust channel estimation for OFDM systems with rapid dispersive fading channels, IEEE Trans. Commun. 46(7):902–915, doi:10.1109/26.701317 | abstract |
| VP84 | Verdú, Poor (1984), On minimax robustness, IEEE Trans. Inf. Theory 30(2):328–340, doi:10.1109/TIT.1984.1056876 | abstract |
| Bor08 | Borio (2008), A statistical theory for GNSS signal acquisition, PhD thesis, Politecnico di Torino | read; grey |
| VW21 | Vovk, Wang (2021), E-values: calibration, combination and applications, Ann. Statist. 49(3):1736–1754, arXiv:1912.06116 | read (preprint) |
| Ram23 | Ramdas, Grünwald, Vovk, Shafer (2023), Game-theoretic statistics and safe anytime-valid inference, Statist. Sci. 38(4):576–601, arXiv:2210.01948 | read (preprint) |
| Sha11 | Shafer, Shen, Vereshchagin, Vovk (2011), Test martingales, Bayes factors and p-values, Statist. Sci. 26(1):84–101, arXiv:0912.4269 | read (preprint) |
| Gru24 | Grünwald, de Heide, Koolen (2024), Safe testing, J. R. Stat. Soc. B 86(5):1091–1128, arXiv:1906.07801 | read (preprint) |
| GV10 | Gross, Vitells (2010), Trial factors for the look elsewhere effect in high energy physics, Eur. Phys. J. C 70:525–530, arXiv:1005.1891 | read (preprint) |
| PK09 | Prix, Krishnan (2009), Class. Quantum Grav. 26, 204013, arXiv:0907.2569 | read (preprint) |
| PS12 | Prix, Shaltev (2012), Search for continuous gravitational waves: optimal StackSlide method at fixed computing cost, Phys. Rev. D 85, 084010, arXiv:1201.4321 | read (preprint) |
| BC00 | Brady, Creighton (2000), Phys. Rev. D 61, 082001, arXiv:gr-qc/9812014 | read (preprint) |
| PC14 | Pletsch, Clark (2014), Astrophys. J. 795, 75, arXiv:1408.6962 | read (preprint) |
| Wet23 | Wette (2023), Searches for continuous gravitational waves from neutron stars: a twenty-year retrospective, Astropart. Phys. 153, 102880, arXiv:2305.07106 | read (preprint) |
| Suv16 | Suvorova, Sun, Melatos, Moran, Evans (2016), Phys. Rev. D 93, 123009, arXiv:1606.02412 | read (preprint) |
| Mel21 | Melatos, Clearwater, Suvorova, Sun, Moran, Evans (2021), Phys. Rev. D 104, 042003, arXiv:2107.12822 | read (preprint) |
| Sun18 | Sun, Melatos, Suvorova, Moran, Evans (2018), Phys. Rev. D 97, 043013, arXiv:1710.00460 | read (preprint) |
| AR18 | Andersen, Ransom (2018), A Fourier domain "jerk" search for binary pulsars, Astrophys. J. Lett. 863, L13, arXiv:1807.07900 | read (preprint) |
| Ran02 | Ransom, Eikenberry, Middleditch (2002), Fourier techniques for very long astrophysical time-series analysis, Astron. J. 124:1788–1809, arXiv:astro-ph/0204349 | read (preprint) |
| JK02 | Johnston, Krishnamurthy (2002), IEEE Trans. Aerosp. Electron. Syst. 38(1):228–242, doi:10.1109/7.993242 | abstract |
| TE96 | Tonissen, Evans (1996), IEEE Trans. Aerosp. Electron. Syst. 32(4):1440–1451, doi:10.1109/7.543865 | abstract |
| Hum05 | Humphreys, Psiaki, Kintner, Ledvina (2005), GPS carrier tracking loop performance in the presence of ionospheric scintillations, ION GNSS 2005 | read; grey (conference) |
| Lop10 | Lopes, Satorius, Estabrook, Sayed (2010), IEEE Trans. Aerosp. Electron. Syst. 46(4):1865–1879, doi:10.1109/TAES.2010.5595600 | read |
| PB88 | Parent, Bourdillon (1988), IEEE Trans. Antennas Propag. 36(1):127–135, doi:10.1109/8.1083 | abstract |
| HC93 | Howland, Cooper (1993), IEE Proc. F 140(1):29–36, doi:10.1049/ip-f-2.1993.0004 | abstract |
| Bou87 | Bourdillon, Gauthier, Parent (1987), Radio Science 22(2):313–320, doi:10.1029/RS022i002p00313 | abstract |
| Li14 | Li, Wei, Guo, Xu, Wang, Tang (2014), The Scientific World Journal 2014, 693872, doi:10.1155/2014/693872 | read (extraction) |
| BG05 | Blanchard, Geman (2005), Hierarchical testing designs for pattern recognition, Ann. Statist. 33(3):1155–1202, arXiv:math/0507421 | read (preprint) |
| Tan95 | Tantaratana, Lam, Vincent (1995), IEEE Trans. Commun. 43:1738–1745, doi:10.1109/26.380224 | abstract |
| Mag65 | Magill (1965), Optimal adaptive estimation of sampled stochastic processes, IEEE Trans. Autom. Control 10(4):434–439, doi:10.1109/TAC.1965.1098191 | abstract |
| DP20 | Dreissigacker, Prix (2020), Phys. Rev. D 102, 022005, arXiv:2005.04140 | abstract |
| JP23 | Joshi, Prix (2023), Phys. Rev. D 108, 063021, arXiv:2305.01057 | abstract |
| Wei21 | Wei, Yuan, Li, Yuan et al. (2021), Off-grid channel estimation with sparse Bayesian learning for OTFS systems, arXiv:2101.05629 | read; grey (preprint) |

### Multi-carrier references, comparable systems and waveform alternatives

| Key | Source | Status |
| --- | --- | --- |
| Dav87 | Davarian (1987), Mobile digital communications via tone calibration, IEEE Trans. Veh. Technol. 36(2):55–62, doi:10.1109/T-VT.1987.24100; US Patent 4,675,880 | abstract; patent text read |
| Li08 | Li, Zhou, Stojanovic, Freitag, Willett (2008), IEEE J. Oceanic Eng. 33(2):198–209, doi:10.1109/JOE.2008.920471 | read |
| BL08 | Borio, Lo Presti (2008), Data and pilot combining for composite GNSS signal acquisition, Int. J. Navig. Obs. 2008, 738183, doi:10.1155/2008/738183 | read |
| Q65 | Taylor, Somerville, Franke, Palermo (2021), Quick-Start Guide to Q65, [wsjt.sourceforge.io](https://wsjt.sourceforge.io/Q65_Quick_Start.pdf); WSJT-X source `lib/qra/q65/` | read; grey |
| FST4 | Franke, Somerville, Taylor, Quick-Start Guide to FST4 and FST4W, [wsjt.sourceforge.io](https://wsjt.sourceforge.io/FST4_Quick_Start.pdf) | read; grey |
| FT8 | Franke, Somerville, Taylor (2020), The FT4 and FT8 communication protocols, QEX July/August 2020 | read; grey |
| Tay05 | Taylor (2005), The JT65 communications protocol, author's manuscript of the QEX article | read; grey |
| Gre | Greenman, MT-63 (ARRL web page); Greenman and Wassilieff, DominoEX pages | read (extraction); grey |
| Gau22 | Gaudio, Colavolpe, Caire (2022), OTFS vs. OFDM in the presence of sparsity: a fair comparison, IEEE Trans. Wireless Commun. 21(6):4410–4423, doi:10.1109/TWC.2021.3129975 | read |
| Sur19 | Surabhi, Augustine, Chockalingam (2019), IEEE Trans. Wireless Commun. 18(6):3049–3063, arXiv:1808.07747 | read (preprint) |
| Zem18 | Zemen, Hofer, Löschenbrand, Pacher (2018), Iterative detection for orthogonal precoding in doubly selective channels, arXiv:1710.09912 | read; grey (preprint) |
| OZ16 | Ouyang, Zhao (2016), Orthogonal chirp division multiplexing, IEEE Trans. Commun. 64(9):3946–3957, arXiv:1601.06576 | read (preprint) |
| Dor19 | Doroshkin et al. (2019), IEEE Access 7:75721–75731, doi:10.1109/ACCESS.2019.2919274 | abstract |
| Zad22 | Zadorozhny et al. (2022), IEEE Access 10:100006–100023, doi:10.1109/ACCESS.2022.3207762 | abstract |

### In-house records cited as [H]

Under `docs/validation-data/triad-v3/`: `2026-11-01-diversity`, `2026-11-02-minus60`, `2026-11-02-spread` (and its `theory/`), `2026-11-02-selective`, `2026-11-02-selective-correction`, `2026-11-03-broad` (and `evidence/theory.md`), `2026-11-05-stochastic`, `2026-11-06-duration-planner`, `2026-11-06-gentle-tracking`, `2026-11-06-realtime`. Maintained guides: [the Robust operating guide](../../../docs/indexes/robust.md), [current Robust architecture](../../../docs/robust-current.md), [channel models](../../../docs/channel-models.md), [validation](../../../docs/validation.md), [triad-realtime](../../../docs/triad-realtime.md) and [the first three-carrier experiment](../../../docs/triad-modem.md).

Section 7.1 also uses the planner receipts `local-evidence/rate-spread-burst/receipts/plan-r1-base.json` to `plan-r8-base.json`, recorded by other work on 2026-11-12 and described in the validation guide under "Manual rate, spreading and burst trends". They are temporary material and may be removed.

### Looked for and not found or not verified

- The STANAG 4415 performance table; the primary Watterson report and paper; per-path tables of the DAMSON campaign; several over-the-horizon-radar sources on phase-path statistics (Fabrizio's book among them).
- Any measured HF Doppler *rate*; any measurement of the stationarity time of HF Doppler or delay spread beyond the one quiet-day record in [Jon86]; any HF replay benchmark or validated time-varying HF channel model.
- NVIS Doppler spread measurements; peer-reviewed EME Doppler spread versus frequency; VLF phase statistics below 9 kHz.
- A quantified detection loss from designing for the bound; a closed-form coherent loss under random-walk frequency; an acquisition demonstration at or below 0 dB-Hz under random Doppler.
- Textbook statements of optimum diversity (Proakis; Wozencraft and Jacobs; Kennedy) and of low-energy-coherence receivers (Van Trees Part III), which could not be opened.
- A published system using a few adjacent simultaneous carriers to cancel common Doppler or to reduce search at these signal levels.

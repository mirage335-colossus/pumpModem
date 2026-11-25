# The fading coherence limit and alternatives to MFSK

25 November 2026 · Research, independent calculations and literature reconciliation

This study examines two linked questions for Matrix Modem. First, is the quoted “hard limit in disturbed channels” correct, and how general is it? Second, has any study shown something substantially better than MFSK for weak signals on disturbed channels, in receiver computation, in sensitivity, or both? It reproduces the limit independently, tests how far it depends on its assumptions, measures how closely practical FFT tone receivers and several alternatives approach it, and reconciles the results with the literature. The calculations are in [`_appraisal/algorithms-matrixModem/coherence-limits/`](../../../_appraisal/algorithms-matrixModem/coherence-limits/README.md). Fielded systems and the computation literature are reviewed in [MFSK alternatives: field practice and receiver computation](mfsk-alternatives-practice-and-computation.md), and the channel evidence behind the model choices is in [HF channel structure and extremes](hf-channel-structure-and-extremes.md).

The design preference stated for this work is to accept a modest, uniform loss of throughput, computation or efficiency in exchange for avoiding a drastic sensitivity or compute failure in any situation, including situations that are only theoretically possible. The conclusions below are organized around that preference. They are research findings, not a qualified receiver: acquisition, nonstationarity, impulsive noise and a complete coded implementation remain to be measured.

## Answer in brief

The claim examined, as quoted for this work:

> There is a hard limit in disturbed channels. What matters is how much signal energy arrives within one fading coherence interval. For a power-limited transmitter, a known formula caps the achievable rate, and my independent simulation of long-symbol tone signaling reaches 97–99.8% of it. At −10 dB-Hz the minimum energy per bit is about 5.4 dB at 0.1 Hz of Doppler spread, 14 dB at 1 Hz, 24 dB at 10 Hz and 29 dB at 30 Hz. No waveform or extra computation beats this, assuming Rayleigh fading and a smooth Doppler spectrum.

No study found shows anything substantially better than well-implemented MFSK on Doppler-spread channels, in sensitivity or in receiver computation, and the theory explains why. The quoted limit is correct and, within its model, close to a worst case. Most of the large losses seen in practice come from the receiver, not from MFSK itself.

**The quoted limit.** It reproduces independently: 5.39, 14.09, 23.92 and 28.68 dB minimum `Eb/N₀` at −10 dB-Hz for Gaussian spectra of `2σ` = 0.1, 1, 10 and 30 Hz. It assumes a constant-envelope (peak-limited) transmitter, frequency-flat Rayleigh fading with Doppler spectrum `S(f)`, one receive antenna, no channel knowledge at either end, known timing and Gaussian noise. Under those assumptions every waveform and receiver is bounded by `R ≤ P − ∫ln(1 + P·S(f)) df` nat/s, where `P` is the received C/N₀ in hertz. [The bound](#the-bound-and-what-it-assumes).

- **What sets it.** The governing quantity is the received energy per effective coherence interval, `x = (C/N₀)/B_eff` with `B_eff = 1/∫S²`. Once it is small, each dB of lost C/N₀ costs about 1 dB more energy per bit, and halving transmit power quadruples the time. That holds for the 1–30 Hz rows; the 0.1 Hz row is near the knee, where the penalty is milder.
- **The table is for one C/N₀ only.** At −20 dB-Hz with 1 Hz of spread, the bound needs about 29 days per 100 bits. The weaker half of the 2 mW–100 mW, 150–180 dB design space, below about −16 dB-Hz, cannot carry a short message within a day once the spread reaches 0.5 Hz. Here a short message is 100 bits. Neither waveform choice nor computation changes that.
- **Plausible transmit time, multi-day included.** More than a day is acceptable. With a practical-receiver allowance and two-thirds path availability, 100 bits at 2–100 mW and 150 dB or more take minutes to hours at 0 dB-Hz or more. From −10 to −22 dB-Hz they take 2 hours to 4 weeks on quiet paths and a day to 9 months on moderate ones. At −30 dB-Hz or less they take more than a year. Over days, the quietest and strongest periods carry most of the message. [Plausible transmit time](#plausible-transmit-time-at-2100-mw-and-150-db-or-more). Only more peak power or receive aperture, lower noise, or a narrower or partly specular path does. [In HF terms](#how-hard-the-limit-is-in-hf-terms).
- **It is a time limit, not a power floor, and recognizing the keyed pattern obeys it too.** The bound is the divergence between "keyed tone present" and "noise only", per unit energy. Under unknown Rayleigh fading the keyed tone only raises its bin's variance, by `x` per coherence interval, so each look adds about `x²/2` nat. Detecting one known pattern at `P_FA` = 10⁻⁶ takes about a quarter of the time of 100 bits at every spread: 7.5 days at −20 dB-Hz and 1 Hz, against 30 min without fading. [Recognizing the keyed pattern](#recognizing-the-keyed-pattern-obeys-the-same-limit).
- **It is close to a worst case.** For a given Doppler autocorrelation, zero-mean Gaussian (Rayleigh) fading is the worst fading law in the capacity-per-unit-energy sense. That is an ergodic statement; on a slow channel a non-Gaussian law can still have worse outage. Koch and Lapidoth proved this only at high SNR; the low-SNR version used here is a derivation from the same step, not a published theorem. For a given RMS spread, the Gaussian spectrum is within 0.1–0.2 dB of the worst possible shape. [Worst-case status](#is-it-a-worst-case).

**Where the bound is too pessimistic.** Any specular power changes it enormously: a Rician `K` of 0.1, about 9% of the received power, takes the 30 Hz figure from 28.7 dB to 8.8 dB. Power confined to a narrow spectral line, or a spectrum that is peaked rather than smooth, also helps: the bound penalizes the spread of power, not the presence of Doppler. Quiet one-hop HF paths often measure as a few narrow, slowly drifting lines, much narrower than the Gaussian reference profiles. Extra receive antennas help, especially polarization diversity, as does a transmitter allowed to burst above its average power. [Where the channel can be better](#where-the-real-channel-can-be-better).

**Where it is too optimistic.** Several effects make reality harder. Finite messages need extra time. Paths are nonstationary and have outages. Unknown timing costs energy: about 1 dB for 2¹⁸ candidate start times with a 72-bit message, more for shorter messages. An unknown spread is costly for a fixed receiver. Two similar rays fade worse than Rayleigh, and impulsive noise can defeat energy detectors. [Where reality is worse](#where-reality-is-worse-than-the-bound).

**Practical receivers against the bound.** Gaps are extra transmit time at equal power; at weak signals the equivalent power penalty is about half as many dB. [Practical receivers](#practical-receivers-against-the-bound), [alternatives](#tone-versus-chirp-and-other-spread-signals).

- **Adapted long-dwell tones reach the bound.** Long dwells with a receiver weighted to the actual Doppler spread come within 0.2 dB of it in every Gaussian spread category tested, in the large-alphabet limit. That receiver, the weighted periodogram, is the long-observation form of the estimator-correlator, and Q65 already uses it in the field.
- **Finite band.** In a real 2.2 kHz band, one configuration (22 tones at 100 Hz spacing) stays within about 1.1 dB across 0.1–30 Hz spreads at −10 dB-Hz. This requires the receiver to adapt its spread weighting over the message.
- **Fixed short symbols are the big loss.** FT8-style 160 ms single-bin detection needs 14 dB more time than the bound on a 0.1 Hz path at −10 dB-Hz.
- **A fixed spread assumption is fragile.** Designing for the wrong spread costs up to 20 dB, and picking the spread separately in each symbol costs 3–11 dB at low SNR. The receiver must adapt over the whole message.
- **Chirps** are 1.3–1.5 dB of power worse than tones in two-path Rayleigh fading at low SNR, because they separate the paths and pay to learn each one. Noise-like spreading overspreads.
- **PSK** can reach the bound in theory, but only with long-memory joint channel estimation and detection. Plain differential PSK reaches 23% of the bound's rate in the low-SNR limit.

**Computation.** An FFT tests every tone hypothesis at once, and the spread-matched statistic is a small weighted sum of bin energies, so MFSK is already near the minimum cost per hypothesis. No study found an order-of-magnitude saving at equal sensitivity for weak signals of uncertain frequency. In every domain surveyed, the number of timing, frequency, drift and spread hypotheses dominates both cost and threshold. [Computation](#receiver-computation).

**Theoretical weaknesses and drastic-failure risks, with their remedies:**

- **Impulsive noise** can collapse an energy detector. Clip or blank before the transform and use robust per-bin statistics.
- **Specular paths** need a long-coherence, drift-tracking receiver branch, limited by the station's reference oscillator as well as the path.
- **Narrow tone spacing** fails in fast fading when dwells are long. Make the spacing several times the widest Doppler spread to be survived.
- **Short dwells** chosen for a stronger signal than actually arrives cost sensitivity. A dwell that turns out too long only caps throughput, so configure for the weakest signal the purpose must survive.
- **Approximate drift search** loses 1.4–2.5 dB within its design range and collapses to about −14 dB beyond it. Search drift exactly.
- **Coarse acquisition grids** lose up to about 3.9 dB in frequency and 2.5 dB in timing. Keep grid losses below the margins being compared.

**For Matrix Modem**, the evidence favors phase-continuous, constant-envelope MFSK at the highest available peak power. It should have tone spacing wider than the worst configured Doppler occupancy, a dwell configured from the target C/N₀, and keyed hopping across the band. Its receiver should adapt its spread weighting over the message rather than per symbol. The [implications section](#implications-for-matrix-modem) gives the reasons and the conditions under which this choice would be wrong. The [distinctions section](#distinctions-to-keep-in-view) lists the quantities most easily confused when these results are reused.

**Not established.** The calculations assume known timing and ideal interleaving. Acquisition, coding, physical completion and a native receiver are untested. Measured HF spectra finer than 0.1 Hz, and specular fractions, are scarce in the accessible literature. Better designs may still exist. [Open questions](#open-questions-and-limits-of-this-study).

## The bound and what it assumes

**Statement.** Take complex baseband `Y(t) = H(t)·X(t) + N(t)` with `H` a stationary, unit-power fading process with normalized Doppler spectrum `S`, `N` white with density `N₀`, and `|X(t)|² ≤` peak. Sethuraman and Hajek give the capacity per unit energy under a peak constraint, and their converse bounds the rate at any average power by `P_avg·η(P_peak)` with `η(P) = 1 − (1/P)∫ln(1 + P·S_ac(f)) df` [Sethuraman and Hajek, IEEE TIT 51(9), 2005, Prop. 3.3, full text](https://hajek.ece.illinois.edu/Papers/SethuramanHajek05.pdf). `S_ac` is the absolutely continuous part of the spectrum. With average equal to peak, `R ≤ P − ∫ln(1 + P·S_ac) df`. Constant-envelope FSK with long symbols achieves it in the infinite-bandwidth limit [Viterbi, IEEE TIT 13(3), 1967](https://doi.org/10.1109/TIT.1967.1054039), [Durisi, Bölcskei and Shamai, ISIT 2007](https://arxiv.org/abs/0705.1682). A discrete-time firm bound valid at every SNR, slightly tighter than this one, is Proposition 2.2 of [Sethuraman, Wang, Hajek and Lapidoth, IEEE TIT 55(4), 2009, full text](https://arxiv.org/abs/0712.2872). The [earlier audit](information-limits-review.md) covers the derivation and the 97–99.8% Monte Carlo ratio; it is not repeated here.

**Independent reproduction.** `bound_shapes.cpp` uses adaptive quadrature rather than the earlier Simpson grid.

| Gaussian `2σ` | `B_eff = 1/∫S²` | rate bound at −10 dB-Hz | minimum `Eb/N₀` | `x = (C/N₀)/B_eff` |
| ---: | ---: | ---: | ---: | ---: |
| 0.1 Hz | 0.177 Hz | 0.0289 bit/s | 5.39 dB | −2.5 dB |
| 1 Hz | 1.77 Hz | 0.00390 bit/s | 14.09 dB | −12.5 dB |
| 10 Hz | 17.7 Hz | 0.000405 bit/s | 23.92 dB | −22.5 dB |
| 30 Hz | 53.2 Hz | 0.000135 bit/s | 28.68 dB | −27.3 dB |

**One parameter governs it.** For any spectrum of fixed shape the minimum `Eb/N₀` depends only on `x`. For the Gaussian shape:

| `x` | −30 dB | −20 dB | −10 dB | 0 dB | +10 dB | +20 dB | +30 dB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| minimum `Eb/N₀` | 31.4 dB | 21.5 dB | 11.7 dB | 3.7 dB | −0.2 dB | −1.3 dB | −1.55 dB |
| loss versus AWGN | 33.0 dB | 23.0 dB | 13.3 dB | 5.3 dB | 1.4 dB | 0.3 dB | 0.05 dB |

Below about `x = −10 dB` the required `Eb/N₀` rises 1 dB for every dB that `x` falls, because the rate falls as `(C/N₀)²/(2·B_eff)`. This is the quadratic regime. In it, halving transmit power quadruples the time per bit, and the energy per bit doubles. The user's phrase “what matters is how much signal energy arrives within one fading coherence interval” is accurate, with the coherence interval defined by `1/B_eff` of the actual spectrum. [Calculation output](../../../_appraisal/algorithms-matrixModem/coherence-limits/results/bound_shapes.md).

**The quoted table spans two regimes.** The 1–30 Hz entries (`x` ≤ −12.5 dB) are in the quadratic regime, where the scaling rules above hold. The 0.1 Hz entry (`x` = −2.5 dB, `P/B_D` = 1) is near the knee, where halving the power less than quadruples the time. Scaling arguments taken from one row therefore do not transfer to the others without checking `x`. In discrete time, Sethuraman, Wang, Hajek and Lapidoth express the quadratic coefficient through `λ = Σ|R(ν)|²`, which is `∫S²` per sample. With a peak constraint only it is `λ²/8` for `λ` < 2 and `(λ − 1)/2` for `λ` ≥ 2, which they call ephemeral and nonephemeral channels (Cor. 2.1). Lapidoth and Wang's Theorem 1 gives the consistent form `lim C/SNR² = (2φ+1)²/8` for `φ` < 1/2 and `φ` otherwise, with `φ = Σ_{ν≥1}|R(ν)|²`. The asymptote is reached by on-off keyed constant-modulus symbols with uncorrelated phases (FSK or PSK) as the block length grows; i.i.d. inputs are generally suboptimal (Prop. 2.3). The model assumes a square-summable autocorrelation. Band-limited (“nonregular”) spectra are covered, because regularity matters only at high SNR.

**The quoted table is for one C/N₀.** The same spreads need very different `Eb/N₀` at other received levels:

| C/N₀ | `2σ` = 0.1 Hz | 0.5 Hz | 1 Hz | 3 Hz | 10 Hz | 30 Hz |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| −20 dB-Hz | 14.1 dB | 20.9 dB | 23.9 dB | 28.7 dB | 33.9 dB | 38.7 dB |
| −10 dB-Hz | 5.4 dB | 11.3 dB | 14.1 dB | 18.7 dB | 23.9 dB | 28.7 dB |
| 0 dB-Hz | 0.4 dB | 3.4 dB | 5.4 dB | 9.2 dB | 14.1 dB | 18.7 dB |
| +10 dB-Hz | −1.2 dB | −0.3 dB | 0.4 dB | 2.2 dB | 5.4 dB | 9.2 dB |
| +20 dB-Hz | −1.5 dB | −1.3 dB | −1.2 dB | −0.7 dB | 0.4 dB | 2.2 dB |

### How hard the limit is in HF terms

The time to deliver 100 information bits at the bound, with ideal coding, interleaving and synchronization:

| C/N₀ | AWGN | 0.1 Hz | 0.5 Hz | 1 Hz | 3 Hz | 10 Hz | 30 Hz |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| −30 dB-Hz | 19 h | 286 d | 4 yr | 8 yr | 23 yr | 78 yr | 234 yr |
| −20 dB-Hz | 116 min | 3 d | 14 d | 29 d | 85 d | 285 d | 2 yr |
| −10 dB-Hz | 12 min | 58 min | 3.7 h | 7.1 h | 21 h | 3 d | 9 d |
| 0 dB-Hz | 69 s | 110 s | 4 min | 6 min | 14 min | 43 min | 2.1 h |
| +10 dB-Hz | 7 s | 8 s | 9 s | 11 s | 17 s | 35 s | 84 s |

**Short message and single-condition airtime.** In this study a *short message* is 100 information bits, the payload of the tables above, delivered complete. The bound includes no allowance for failure probability. At 1% failure a 100-bit message needs about 1.7–2.1 dB more time at weak signal levels ([finite messages](#where-reality-is-worse-than-the-bound)). More than a day of transmit time is acceptable for Matrix Modem (user decision, 25 November 2026). One diurnal cycle, 24 hours on one configured frequency, is instead the longest airtime over which one C/N₀ and one spread can describe the message, which the stationary bound requires. This study calls airtime within it *single-condition airtime*; longer airtime is estimated [below](#plausible-transmit-time-at-2100-mw-and-150-db-or-more). Shorter reference airtimes mark where the channel description changes:

- **15 minutes** stays within one roughly stationary channel state. Second-order statistics hold for seconds to minutes, traveling ionospheric disturbances act over 15–180 minutes, and flare fadeouts last a mean of 13–39 minutes.
- **3 hours** stays within one propagation regime. The receiver must track drift of the Doppler centroid, and a fadeout or slow fade within the message becomes an erasure.
- **24 hours** spans one diurnal cycle. A fixed frequency can lose the path for hours; one measured 14 MHz path lost it for about 6 h a day. The code must span that closure, and absorption changes C/N₀ through the day.

Beyond a day, delivery depends on availability rather than on this bound. Day-to-day lower deciles fall 8–17 dB below the median, and storms, polar cap absorption and fadeouts add erasures ([channel evidence](hf-channel-structure-and-extremes.md#nonstationarity-and-outages)). That does not rule out multi-day airtime, which the user accepts and the [appraisal](../../../_appraisal/matrixModem.md) expects in important cases. Such airtime needs a time-varying channel model. In the quadratic regime the rate grows as (C/N₀)², so a long message gets most of its evidence from the strongest periods, provided the receiver accumulates evidence across them. A figure at the median C/N₀ is then pessimistic, except where outages dominate. The size of that effect depends on the upper tail of day-to-day variation, which the cited sources quantify less clearly than the lower decile. It has not been computed.

Minimum received C/N₀ in dB-Hz for 100 bits within each airtime, at the bound ([`deadline_cn0.py`](../../../_appraisal/algorithms-matrixModem/coherence-limits/results/deadline_cn0.md)) [derived]:

| Airtime | no fading | 0.1 Hz | 0.5 Hz | 1 Hz | 3 Hz | 10 Hz | 30 Hz |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 15 min | −11.1 | −6.4 | −3.7 | −2.3 | −0.2 | +2.3 | +4.7 |
| 1 h | −17.2 | −10.1 | −7.0 | −5.6 | −3.3 | −0.8 | +1.6 |
| 3 h | −21.9 | −12.8 | −9.5 | −8.1 | −5.7 | −3.2 | −0.8 |
| 24 h | −31.0 | −17.6 | −14.2 | −12.7 | −10.3 | −7.7 | −5.3 |

Under the bound's assumptions no waveform or receiver can go below these floors. A message at 1% failure with a practical receiver needs roughly 1.5–2 dB more C/N₀ in the quadratic regime, and more near the knee.

The [link-budget review](link-budget-and-operation-review.md#what-a-plausible-noise-spread-does-to-airtime) maps 2 mW–100 mW and 150–180 dB of path loss onto −47 to +14 dB-Hz for noise densities of −144 to −130 dBm/Hz. Combined with these floors, the weaker half of that range, below about −16 dB-Hz, cannot carry a short message within a day once the Doppler spread reaches 0.5 Hz. At 0.1 Hz only about its top dB can, and none of it can within 3 hours. Neither the waveform nor the computation changes this. Only three things move a link out of that region: more received energy per coherence interval (higher peak power, lower loss, quieter noise, more receive aperture), a narrower or partly specular channel, or a transmitter allowed to burst above its average power. Receiver computation cannot. A receiver can, however, lose much of what the channel offers, which is where [practical receivers](#practical-receivers-against-the-bound) differ.

### Plausible transmit time at 2–100 mW and 150 dB or more

[`scenario_airtime.py`](../../../_appraisal/algorithms-matrixModem/coherence-limits/results/scenario_airtime.md) estimates wall-clock transmit time for a 100-bit message from the link-budget review's scenario inputs, with unity combined antenna gain [derived]. It multiplies the time at the bound by two stated allowances:

- **Practical receiver: 4 dB of time above the bound.** That allows about 1.8 dB for 1% failure of a 100-bit message, 1.1 dB for the finite-band adaptive MFSK receiver, and 1 dB for an unknown start time and frequency.
- **Availability: two thirds of the time.** The configured frequency is taken to carry the path for two thirds of the time, which allows about 8 hours a day of closure, fadeout or deep slow fade. This is an assumption, not a measurement for any path. Without a return channel the transmitter cannot tell when the path is open, so it transmits throughout, and transmit time equals wall-clock time.

The spreads are Gaussian `2σ`. 0.05 Hz is the narrowest measured quiet one-hop mode width, including equipment. 0.1 Hz is a quiet path, 1 Hz is the F.1487 moderate profile, and 10 Hz is a disturbed low-latitude path.

| Scenario | C/N₀ | no fading | 0.05 Hz | 0.1 Hz | 1 Hz | 10 Hz |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 100 mW, 150 dB, −144 dBm/Hz | +14 dB-Hz | 10 s | 11 s | 11 s | 13 s | 29 s |
| 100 mW, 150 dB, −130 dBm/Hz | 0 dB-Hz | 4.4 min | 5.8 min | 6.9 min | 22 min | 2.7 h |
| 2 mW, 150 dB, −144 dBm/Hz | −3 dB-Hz | 8.7 min | 14 min | 17 min | 74 min | 10 h |
| 100 mW, 170 dB, −140 dBm/Hz | −10 dB-Hz | 44 min | 2.3 h | 3.6 h | 27 h | 11 d |
| 2 mW, 150 dB, −130 dBm/Hz | −17 dB-Hz | 3.6 h | 38 h | 2.9 d | 27 d | 9 mo |
| 100 mW, 180 dB, −140 dBm/Hz | −20 dB-Hz | 7.3 h | 5.8 d | 11 d | 3.5 mo | 2.9 yr |
| 2 mW, 165 dB, −140 dBm/Hz | −22 dB-Hz | 11 h | 14 d | 28 d | 9 mo | 7 yr |
| 100 mW, 180 dB, −130 dBm/Hz | −30 dB-Hz | 3.0 d | 18 mo | 2.9 yr | 29 yr | 293 yr |
| 2 mW, 180 dB, −140 dBm/Hz | −37 dB-Hz | 15 d | 37 yr | 74 yr | 737 yr | — |
| 2 mW, 180 dB, −130 dBm/Hz | −47 dB-Hz | 5 mo | — | — | — | — |

A dash marks more than a thousand years. The table falls into three bands:

- **Minutes to hours at 0 dB-Hz or more.** This covers 100 mW at 150 dB at any of these sites, and 2 mW at 150 dB at a quiet-rural site. Even a 10 Hz disturbed path takes at most about 10 hours.
- **Hours to months from −10 to −22 dB-Hz.** This covers 100 mW at 170–180 dB, and 2 mW at 150–165 dB with rural noise. Quiet paths take 2 hours to 4 weeks, moderate paths a day to 9 months, and disturbed paths 11 days to years. The multi-day allowance matters most here.
- **Not plausible at −30 dB-Hz or less.** This covers 2 mW at 180 dB, and 100 mW at 180 dB with rural noise. Even a quiet path takes more than a year under Rayleigh fading. Only the no-fading column stays within months, and no skywave path holds coherence for days.

**Mixed conditions over a long message.** A message spanning days sees quiet, moderate and disturbed periods. Suppose the receiver keeps soft evidence across them and weights each period by its own estimated spread and C/N₀. Then the evidence adds, and the time is `T ≈ 100·ln 2 / Σ fᵢ·Rᵢ` for fractions `fᵢ` of time at rates `Rᵢ`. In the quadratic regime a tenfold wider spread costs a tenfold lower rate, so the quietest periods carry almost all of the message. The time is close to the quiet-path time divided by the fraction of time the path is quiet. With 0.1 Hz one third of the time and 1 Hz otherwise, it is 2.5 times the 0.1 Hz value [derived]. The same reasoning applies to C/N₀: the strongest periods dominate, so the median figures above are pessimistic for such a receiver. They are optimistic for a receiver that applies one fixed weighting, or one that cannot hold timing, frequency and evidence across outages for days.

**Levers.** In the quadratic regime each dB of C/N₀ is worth 2 dB of time. Receive-site noise is therefore the largest lever in this range: −130 instead of −144 dBm/Hz costs a factor of about 600 in time. Raising 2 mW to 100 mW gains 17 dB of C/N₀, a factor of about 2,500 in time. A second receive branch that fades independently at the same C/N₀, such as the other polarization, halves the time.

**Limits.** These are bound-based estimates with stated allowances, not demonstrated performance. They assume Gaussian Doppler spectra and Rayleigh fading. A partly specular quiet path would be faster, though by less than the [Rician table](#where-the-real-channel-can-be-better) suggests at very weak levels. A line helps only while its phase stays predictable through traveling-disturbance drift and oscillator wander, so at −30 dB-Hz or less it behaves like a very narrow spread rather than a perfect line [inference]. Impulsive noise without blanking, or two-ray fading, would be slower. No Matrix Modem receiver yet keeps evidence across days, and the availability fraction is not measured for any path.

### Recognizing the keyed pattern obeys the same limit

A natural objection is that a receiver holding the key knows which bins should show a rise in the noise floor, so fragments of the keyed MFSK pattern should be recognizable against chance at any power. They are, and the bound agrees. Its rate is positive at every C/N₀, so it sets no minimum power. What it sets is the time needed, and that time grows without bound as power falls. The objection describes the test the bound is built from. For a long constant-envelope dwell, the information divergence rate between "keyed tone present" and "noise only" in the keyed bins is `∫[P·S − ln(1 + P·S)] df`. Because `∫S df = 1`, that divergence rate equals the rate bound `P − ∫ln(1 + P·S) df` itself, in nat/s. Dividing it by the received power gives the capacity per unit energy above. The bound measures how fast that expected rise becomes distinguishable from chance.

Doppler spread slows that test because of what the rise looks like. When neither end knows the Rayleigh fading, the keyed tone in its bin is itself a complex Gaussian process. "Present" means only that the bin's variance is `1 + x` instead of 1 in each effective coherence interval; nothing else distinguishes it. A fade peak has the same exponential law as a noise peak. Noise energy fluctuates by its own mean, so each look adds about `x²/2` nat of evidence when `x` is small, and about `(z₀ + z₁)²/x²` looks are needed. The key tells the receiver where to look. It cannot tell the receiver the channel phase, which changes every `1/B_eff`. On a static channel the complex amplitudes add coherently over the whole pattern, and the time falls as `1/(C/N₀)`. With spread only energies add, and it falls as `B_eff/(C/N₀)²`. Watching for fragments where a fade lifts the signal does worse than the weighted energy sum, which is sufficient for this exponential family. A specular component is different. It gives the rise a coherent structure that noise lacks, which is why a small Rician `K` changes the bound so much ([below](#where-the-real-channel-can-be-better)).

[`presence_detection.py`](../../../_appraisal/algorithms-matrixModem/coherence-limits/results/presence_detection.md) computes the time to detect one known pattern with known timing and frequency at `P_FA` = 10⁻⁶ and `P_D` = 0.9. It uses the optimum quadratic detector, a normal approximation and Gaussian spectra [derived]:

| C/N₀ | no fading | 0.1 Hz | 1 Hz | 10 Hz | 30 Hz |
| ---: | ---: | ---: | ---: | ---: | ---: |
| −20 dB-Hz | 30 min | 18 h | 7.5 d | 75 d | 224 d |
| −10 dB-Hz | 3.0 min | 14 min | 111 min | 18 h | 2.2 d |
| 0 dB-Hz | 18 s | 35 s† | 84 s | 11 min | 33 min |

† About six independent looks. The normal approximation and the ergodic treatment do not hold; outage governs.

In every row with enough independent looks, detection takes about a quarter of the time of a 100-bit message at the bound. Both accumulate evidence at the same divergence rate. Detection needs about `(z₀ + z₁)²/2` ≈ 18 nat, and 100 bits need 69 nat. Knowing that the keyed transmitter is present is therefore about as hard as receiving 26 bits from it, and the spread penalty is the same for both. In the quadratic regime the detection time is about `(z₀ + z₁)²·B_eff/(C/N₀)²`, or `36.4·B_eff/(C/N₀)²` at these probabilities, so halving power quadruples it. Unknown start times or frequencies add about `ln A` nat for `A` candidates, for detection as for a message. Under one key, cryptographic randomness changes where the evidence is, not how fast it accumulates. These times are for the intended receiver, which holds the key. An observer without the key cannot concentrate on the keyed bins, so its detection time is a separate question ([observer detection](observer-and-interference.md), [detection versus decoding](information-limits.md#distinguish-observable-detection-from-decoding)).

The same three escapes apply: more received energy per coherence interval, a narrower or partly specular path, or bursting above average power. One more follows from the bound's assumption that neither end knows the channel. A separately powered reference much stronger than the data would raise the energy available for learning the channel. Then the data could be detected nearly coherently. That works only if the reference shares the data tones' fading: within their coherence bandwidth, on the same propagation modes. Counted with the reference's power, the pair still obeys the bound; pilot-assisted constant-envelope signaling reaches it but does not beat it ([PSK families](#tone-versus-chirp-and-other-spread-signals)). The Matrix Modem appraisal currently keeps Helper Carrier separate from data detection. Whether Helper tones share the data tones' fading closely enough has not been measured [inference]. Delay spread hardly enters for tones whose dwell is much longer than it. Different tones fade differently, but Doppler sets each tone's coherence. Delay spread penalizes waveforms that resolve the paths, such as chirps and wideband chips.

## Is it a worst case?

**Fading law.** For any input `x`, the information divergence that defines capacity per unit energy is `D(P_{Y|x} ‖ P_{Y|0}) = (received energy) − I(H; Y | X = x)`. For a fixed autocovariance of `H`, the Gaussian law maximizes `I(H; xH + N)`, so every non-Gaussian fading law with the same autocorrelation has at least the Rayleigh capacity per unit energy. Koch and Lapidoth proved the corresponding statement for the high-SNR pre-log, with the same maximum-entropy and Szegő step at its core [Koch and Lapidoth, IEEE TIT 56(3), 2010, eqs. 17–18, full text](https://arxiv.org/abs/0902.3372). The low-SNR, per-unit-energy version used here is a derivation from that identity, not a published theorem. Zhang and Mitra supply the achievability side: orthogonal codes with a quadratic receiver that uses only the channel covariance achieve the Rayleigh value for any fading law with that covariance [Zhang and Mitra, arXiv cs/0702108, eqs. 38–41, full text; journal status unverified](https://arxiv.org/abs/cs/0702108). The repository's [large-alphabet calculator](../../../_appraisal/algorithms-matrixModem/coherence-limits/README.md) confirms the same property: the rate of any quadratic tone receiver depends only on second-order channel statistics. Two limits apply. The argument is asymptotic and ergodic. In a slow or finite message, a non-Gaussian law with the same covariance can have more probability near zero amplitude and a worse outage; two comparable discrete rays are the HF example ([channel evidence](hf-channel-structure-and-extremes.md#amplitude-statistics)).

What is proven and what is derived must be kept apart. Koch and Lapidoth's Theorem 1 needs fading with no probability mass at zero. Their Note 1 gives an ergodic law with a mass point at zero and a smaller high-SNR pre-log than Gaussian. Their Note 2 shows the inequality can be strict: memoryless phase noise has pre-log 1/2 where Gaussian fading with the same spectrum has 0. The low-SNR divergence argument above does not need the no-mass-at-zero condition. It does need a coding theorem for the non-Gaussian channel with memory: ergodicity or mixing, a zero-cost symbol and block extension. Zhang and Mitra's model is a memoryless vector channel, so it applies when successive constant-envelope bursts fade independently. A consequence: the quoted figures are lower bounds on the required `Eb/N₀` only for Gaussian fading; a non-Gaussian channel with the same covariance may need less energy, never more, in the ergodic limit.

**Spectral shape.** For a fixed support width, the flat spectrum is worst by Jensen's inequality. For a fixed RMS width, the worst spectrum at a given C/N₀ is the Karush–Kuhn–Tucker solution `S(f) = [1/(a + b·f²) − 1/P]⁺`, which tends to a parabola at low SNR. Minimum `Eb/N₀` at −10 dB-Hz, with each shape scaled to the same nominal spread `W`:

| Shape | W = 0.1 Hz | 1 Hz | 10 Hz | 30 Hz |
| --- | ---: | ---: | ---: | ---: |
| Gaussian, `2σ = W` | 5.39 | 14.09 | 23.92 | 28.68 |
| Worst case for `2σ_rms = W` | 5.48 | 14.28 | 24.14 | 28.90 |
| Flat, `2σ_rms = W` | 5.17 | 13.97 | 23.82 | 28.58 |
| Jakes (U-shaped), `2σ_rms = W` | 3.71 | 10.52 | 18.65 | 22.76 |
| Two narrow components at ±W/2 | 3.41 | 11.25 | 20.93 | 25.68 |
| Lorentzian, same 80%-power width | 4.83 | 12.90 | 22.62 | 27.37 |

The theory agent's independent computation gave the same Gaussian, worst-case, flat and Jakes values. Peaked spectra are better because they concentrate power: the bound penalizes the spread of power, not the presence of Doppler. The Jakes advantage, 1.7–5.9 dB here, depends on sharp spectral edges that only observations of order one over the edge width resolve, and HF channels are not usually modeled with Jakes spectra.

Two shape cases fall outside the simple `B_eff` description. A Jakes (Clarke) spectrum has integrable edge singularities that are not square-integrable, so `∫S²` and `B_eff` are infinite and the quadratic approximation fails. Its low-SNR rate scales as `(2/(π·ω_m))·P²·log(1/P)` [Zhang and Laneman 2007, eq. 32](https://arxiv.org/abs/cs/0508121). More generally, a singularity `|f|^(−α)` with 1/2 < α < 1 gives a rate of order `P^(1/α)` [theory reviewer's derivation]. The Jakes row above comes from the exact expression at −10 dB-Hz, not from `B_eff`. Spectral lines are the other case and are treated [below](#where-the-real-channel-can-be-better). Related formulations: Bergel and Benedetto express low-SNR capacity through an SNR-dependent effective coherence time [arXiv 1107.0639, abs](https://arxiv.org/abs/1107.0639), and for wideband doubly spread channels the first-order coefficient depends on the scattering function only through `κ_H = ∫∫C_H²` [Schuster et al., Theorem 3](https://arxiv.org/abs/0801.1002), the two-dimensional counterpart of `∫S²`. No capacity result for nonstationary spectra was found; every source assumes stationary or WSSUS fading.

**Measured widths must be converted through `B_eff`.** HF measurements report widths at different levels: `2σ` of a Gaussian fit (F.1487), the band containing 80% of power (Stocker, DAMSON), −10 or −30 dB widths (equatorial spread-F studies), or half-power widths. For heavy-tailed shapes these differ greatly. The bound depends on `B_eff = 1/∫S²`, which for unit-integral shapes is:

| Shape | `B_eff` per −3 dB width | per −10 dB width | per 80%-power width | per `2σ_rms` |
| --- | ---: | ---: | ---: | ---: |
| Gaussian | 1.51 | 0.83 | 1.38 | 1.77 |
| Lorentzian (exponential autocorrelation) | 3.14 | 1.05 | 1.02 | — |
| Two-sided exponential | 2.89 | 0.87 | 1.24 | 1.41 |

At an equal −10 dB width, a Lorentzian has about 27% more `B_eff` than a Gaussian, so a Gaussian bound is about 1 dB optimistic in the quadratic regime. At an equal half-power width it is about 3 dB optimistic. At an equal 80%-power width the Lorentzian is the better channel. Measured HF spectra are often peaked with heavy tails ([channel evidence](hf-channel-structure-and-extremes.md#doppler-spectrum-shape)), so the safest input to the bound is `B_eff` computed from a measured spectrum, or the −10 dB width with the Gaussian conversion.

**Claims to approach or beat the bound.** A limited 2018–2026 search found no result that beats the bound under the same assumptions; this is not proof of absence. The results that do better than orthogonal FSK all change the model or only match it. Constant-envelope PSK with recursive training matches it ([below](#tone-versus-chirp-and-other-spread-signals)). Massive-SIMO energy detection matches coherent scaling only with many receive antennas [Chowdhury, Manolakos and Goldsmith, IEEE TIT 2016, abs]. Kowshik and Polyanskiy treat quasi-static fading in a many-user access channel [IEEE TIT 67(9), 2021, abs]. A superimposed-pilot lower bound for block fading finds the optimal pilot fraction tending to one half at low SNR and claims nothing beyond the bound [Pastore, arXiv 1804.07114](https://arxiv.org/abs/1804.07114). Asyhari, Koch and Guillén i Fàbregas (2011) treat only the high-SNR pre-log.

## Where the real channel can be better

**Specular or line components.** A spectral line contributes no learning penalty: its power is usable at the coherent efficiency once its phase and frequency are tracked. With a Rician factor `K` and a Gaussian diffuse part of `2σ = W`, at −10 dB-Hz:

| `K` | W = 0.1 Hz | 1 Hz | 10 Hz | 30 Hz | limit `(1 + 1/K)·ln 2` |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0 (Rayleigh) | 5.39 | 14.09 | 23.92 | 28.68 | — |
| 0.1 | 4.25 | 7.86 | 8.71 | 8.79 | 8.82 |
| 0.3 | 2.88 | 4.48 | 4.75 | 4.77 | 4.78 |
| 0.5 | 2.06 | 3.02 | 3.16 | 3.17 | 3.18 |
| 1 | 0.94 | 1.36 | 1.41 | 1.42 | 1.42 |
| 3 | −0.43 | −0.35 | −0.34 | −0.34 | −0.34 |
| 10 | −1.19 | −1.18 | −1.18 | −1.18 | −1.18 |

At weak signal levels the diffuse power is nearly wasted and the specular fraction sets the efficiency. This agrees with the low-power Rician result `Eb/N₀,min = ln 2/[1 − ln(1+η)/(η(K+1))]` of [Gursoy, Poor and Verdú, IEEE TWC 4(5), 2005, Prop. 3, full text](https://arxiv.org/abs/cs/0501067), which assumes the specular mean is known. Spectral lines are excluded from the Sethuraman–Hajek ergodic model, and a random-phase line is not ergodic. An unknown fixed phase costs a vanishing amount per unit time asymptotically but matters in finite messages. Lapidoth and Wang remark, citing a thesis rather than proving it, that with spectral lines capacity typically grows linearly with SNR, with slope equal to the total power in the lines [Lapidoth and Wang, arXiv cs/0604031](https://arxiv.org/abs/cs/0604031). The table extends the Gursoy–Poor–Verdú expression to a Gaussian diffuse spectrum through the same divergence identity, `C_p = 1 − (1/P)∫ln(1 + P·S_d)`, with `∫S_d = 1/(K+1)`; for a flat `S_d` it reduces exactly to their Proposition 3, which two-point on-off signaling at the peak achieves. All of it assumes the specular amplitude, phase and Doppler are known. A narrow but nonzero component behaves similarly: 30% of power in a 0.05 Hz component and 70% in a 10 Hz component needs 12.1 dB at −10 dB-Hz instead of 23.9 dB.

Peakiness interacts with specular power. In memoryless Rician fading with a fourth-moment or peak-to-average constraint `κ`, the wideband slope is `2K²/((1+K)² − κ)`, and when `κ` > `(1+K)²` the minimum `Eb/N₀` occurs at a nonzero spectral efficiency [Gursoy, Poor and Verdú 2005, Cor. 1 and Prop. 2](https://arxiv.org/abs/cs/0501067). On a strongly specular path the lowest-rate operating point is then not the most energy-efficient one, unlike the Rayleigh case.

Measured quiet mid-latitude one-hop paths are much more coherent than the Gaussian reference profiles: FST4W measurements show mode widths near 0.06 Hz at 14 MHz over 1535 km, including equipment, and stable-carrier Doppler records show discrete traces wandering slowly by up to about ±1 Hz per day ([channel evidence](hf-channel-structure-and-extremes.md#coherence-on-quiet-paths)). Sporadic-E and single stable modes are often Rician. On such paths the Rayleigh bound is pessimistic, but only a receiver with a long-coherence, drift-tracking branch can collect the difference. The [receiver results](#practical-receivers-against-the-bound) show how hard that is: even the matched long-dwell tone receiver stays 2.1 dB from the bound on a `K = 1`, 1 Hz channel at −10 dB-Hz with an 82 s dwell, because a per-symbol noncoherent tone cannot exploit phase memory longer than its dwell.

**Multiple receive branches.** Information adds across independently fading branches, so `n_R` independent branches multiply the bound's rate by `n_R`: about 3 dB less time per doubling, or about 1.5 dB of power in the quadratic regime. Fully correlated branches add coherently, giving 3 dB of power per doubling [Sethuraman, Wang, Hajek and Lapidoth 2009, Prop. 2.4 and Cor. 2.2–2.3](https://arxiv.org/abs/0712.2872). The per-branch C/N₀ reduction that keeps the single-antenna rate at −10 dB-Hz is 1.5–1.7 dB for two independent branches and 3.0–3.4 dB for four, against 3.0 and 6.0 dB for fully correlated branches; the larger independent-branch figures are at 0.1 Hz, outside the quadratic regime ([`branch_power.py`](../../../_appraisal/algorithms-matrixModem/coherence-limits/literature-checks/results/branch_power.txt)). The firm bound becomes a sum over branches, each paying its own `∫ln(1 + ρS_r)`. Transmit antennas differ. Under a total-power constraint they do not help and one antenna should be used. Under per-antenna constraints, as with separately peak-limited amplifiers, the low-SNR coefficient grows as `n_T²·n_R` (SWHL eqs. 58–59), because each amplifier adds power and the quadratic regime rewards power quadratically. In the wideband noncoherent regime, correlation between receive branches is beneficial, the opposite of coherent systems [Schuster, Durisi, Bölcskei and Poor, arXiv 0801.1002, Theorem 3](https://arxiv.org/abs/0801.1002). Polarization is the natural HF branch: a linear antenna receives half of each circular magneto-ionic mode, while a dual-circular receiver can separate them, with more than 25 dB O/X isolation reported on NVIS ([channel evidence](hf-channel-structure-and-extremes.md#polarization-and-magneto-ionic-fading)). At HF, external noise can be correlated between co-located antennas, so the array gain against noise depends on noise directionality and must be measured.

**Transmitter peakiness.** The minimum `Eb/N₀` depends on the received peak C/N₀, not on the average. If the amplifier may exceed an average-power budget by a factor `κ` during bursts, at −10 dB-Hz average and 10 Hz spread the requirement falls from 23.9 dB (`κ` = 1) to 20.9, 18.0, 14.1 and 5.4 dB for `κ` = 2, 4, 10 and 100. For a fixed peak (PEP) limit, constant-envelope transmission at the peak is optimal and duty-cycling only spends time. Two consequences for operation: always transmit at the highest available peak power, and splitting a fixed peak between two simultaneous tones halves the rate in the quadratic regime (ratio 0.50–0.58 at −10 dB-Hz).

## Where reality is worse than the bound

**Finite messages.** The bound is an ergodic rate. Using the receiver's information density (an exact Gamma law for a flat spectrum, moment-matched for Gaussian; a random-coding heuristic, not a converse), the extra time needed for a message to fail with probability `ε`:

| `x` | 20 bits, ε = 1% | 100 bits, 1% | 1000 bits, 1% | 100 bits, 0.1% |
| ---: | ---: | ---: | ---: | ---: |
| −20 dB | 3.7 dB | 1.7 dB | 0.5 dB | 2.3 dB |
| −10 dB | 3.8 dB | 1.8 dB | 0.6 dB | 2.3 dB |
| 0 dB | 4.5 dB | 2.1 dB | 0.7 dB | 2.8 dB |
| +10 dB | 7.2 dB | 3.8 dB | 1.3 dB | 4.7 dB |
| +20 dB | 12.4 dB | 7.7 dB | 3.2 dB | 9.0 dB |
| +30 dB | 19.5 dB | 13.8 dB | 7.3 dB | 15.4 dB |

At weak signal levels the penalty depends almost only on message length and reliability, because the message spans thousands of coherence intervals. The bound's optimism is largest where the energy per coherence interval is high and a short message spans few fades: a slow path at moderate C/N₀. No published low-SNR finite-blocklength result for stationary noncoherent fading was found; a 2026 paper notes the gap and gives computable information-density bounds for Gauss–Markov fading, with numerical results only at 20 dB SNR [Sun, Perlaza, Mary and Gorce, arXiv 2604.16062](https://arxiv.org/abs/2604.16062). The block-fading normal approximations are high-SNR results, accurate above about 15 dB with ten or more coherence intervals [Lancho, Koch and Durisi, IEEE TIT 66(1):496–519, 2020, abs](https://doi.org/10.1109/TIT.2019.2945782), with a MIMO extension by Qi and Koch [arXiv 2503.01504, sec](https://arxiv.org/abs/2503.01504). Error-exponent results (Kennedy, Viterbi, Hajek and Subramanian) remain the finite-length reference for this regime. [Calculation output](../../../_appraisal/algorithms-matrixModem/coherence-limits/results/finite_message.md).

A second finite-length effect is distinct from codeword error probability: the loss from a finite constant-envelope burst (one dwell) relative to the infinitely long burst. It depends only on `T·2σ` and is almost the same for all four reference spreads at −10 dB-Hz ([`finite_burst.cpp`](../../../_appraisal/algorithms-matrixModem/coherence-limits/literature-checks/results/finite_burst.txt)):

| `T·2σ` | 1 | 2 | 5 | 10 | 20 | 100 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| loss of capacity per unit energy | 0.66–0.86 dB | 0.32–0.41 dB | 0.12–0.16 dB | 0.06–0.08 dB | 0.03–0.04 dB | 0.01 dB |

**Fading more severe than Rayleigh.** Two discrete rays of similar strength (O and X on north–south or NVIS paths, high and low rays near the MUF, short and long path) produce deterministic deep nulls with Nakagami `m` between 0.5 and 1. Their spectrum is line-like, so the ergodic bound is favorable, but outage over a finite message is worse than Rayleigh. Polarization matching, frequency hopping and interleaving over the beat period break this structure.

**Nonstationarity and outages.** Second-order channel statistics are roughly stationary over seconds to minutes; traveling ionospheric disturbances move the Doppler centroid by 0.1–1 Hz over 15–180 minutes and can change mode power by 20–30 dB near focusing cusps. Shortwave fadeouts last a mean of 13–39 minutes for C1–X10 flares, with 90th percentiles of 30–123 minutes. Polar cap absorption can last about ten days on polar paths, and storm nights can close the F2 layer. Day-to-day lower deciles of signal level fall 8–17 dB below the monthly median ([channel evidence](hf-channel-structure-and-extremes.md#nonstationarity-and-outages)). None of these is inside a stationary fading bound. They belong to an availability and erasure layer and to the receiver's ability to track and reacquire.

**Unknown timing and spread.** Locating a message among `A` possible start times costs energy for about `log₂A` extra bits in the capacity-per-unit-cost sense: 72 bits with 2¹⁸ candidate starts cost about 1 dB more energy [Chandar, Tchamkerten and Tse, IEEE TIT 59(3), 2013, Theorem 2](https://arxiv.org/abs/1007.4872); [Polyanskiy, IEEE TIT 59(3), 2013](https://people.lids.mit.edu/yp/homepage/data/async.pdf). This extends to fading only through the same divergence formula. With a zero-cost idle symbol the minimum energy is `(B + log₂A)` times the per-bit synchronous cost, so energy rises by the factor `1 + log₂A/B`, which vanishes only if `log A` grows more slowly than the message size `B`; a known arrival window `d` replaces `A` by `A/d`. The model covers source asynchrony, an unknown start time, not clock or frequency offsets, which add their own hypotheses. Polyanskiy adds that demanding exact synchronization costs no capacity and that universal codes achieve the asynchronous capacity; at a fixed rate, asynchrony up to `2^(n·A₁)` costs neither capacity nor dispersion, but `A₁` is tiny at low SNR, so the per-unit-cost framing is the relevant one here.

An unknown Doppler spectrum costs no first-order rate with a universal decoder, by analogy with [Feder and Lapidoth, IEEE TIT 44(5), 1998](https://doi.org/10.1109/18.705540), but no theorem for continuous stationary noncoherent fading was found. A fixed receiver is different. Zhang and Mitra show that the compound capacity per unit cost is a sup–inf over inputs and channels, while a fixed quadratic receiver under covariance uncertainty achieves only an inf–sup that can be smaller unless a saddle point exists; with identity weighting the rate is set by the worst channel's largest eigenvalue (Example 5, eq. 43). This is the theoretical counterpart of the mismatch losses [below](#robustness-to-unknown-spread). A per-burst calculation also shows that `1 − ln det(I + P·K_T)/(P·T)` decreases monotonically to its limit as the burst length `T` grows, for every spectrum, so long constant-envelope bursts are near-optimal for all spectra at once; the cost of not knowing the spectrum then appears only in finite-length terms and in computation. A 2026 universal decoder for a fixed unknown Gaussian MIMO-ISI channel matches the maximum-likelihood exponent, but for a fixed channel rather than random fading [Elkayam and Feder, arXiv 2611.09383, abs](https://arxiv.org/abs/2611.09383).

**Impulsive noise.** For a fixed noise power, non-Gaussian noise is expected to increase the information limit, but a receiver built on Gaussian energy statistics may not collect it. The expectation rests on neighboring results, not on a theorem for this channel. Gaussian noise is the worst additive noise of a given covariance for average-power inputs, with no peak constraint and no fading [Diggavi and Cover, IEEE TIT 47(7):3072–3081, 2001](https://doi.org/10.1109/18.959289). Weak-signal capacity is set by the noise Fisher information, which Gaussian noise minimizes for a fixed variance [Duan, Chapeau-Blondeau and Abbott, Sci. Rep. 6:27946, 2016, sec](https://doi.org/10.1038/srep27946). No proof that Gaussian noise is worst for the peak-limited noncoherent fading channel was found. With orthogonal codes, a linear receiver's rate is guaranteed for any noise of the same variance, while a quadratic (energy) receiver's rate can tend to zero in Gaussian-mixture impulsive noise [Zhang and Mitra, Example 3, eqs. 31–32](https://arxiv.org/abs/cs/0702108). MFSK energy detection is such a quadratic receiver. HF noise is impulsive and non-Gaussian, varies by roughly ±10 dB between sites and deciles, and includes narrowband carriers ([channel evidence](hf-channel-structure-and-extremes.md#noise-and-interference)). This is a genuine theoretical weakness of MFSK energy detection that needs clipping, blanking or a locally optimum nonlinearity before the transform, and a robust per-bin metric. Locally optimum detectors save signal power in Middleton Class A noise [Spaulding and Middleton 1977, sec]; no capacity-per-unit-energy result combining fading with Class A noise was found.

**Long echoes.** Round-the-world (about 133–140 ms) and short-path/long-path (tens of milliseconds) echoes are rare but can be within a few dB of the direct signal. With 160 ms symbols they fall mostly into the next symbol's window. With multi-second tones they mostly overlap their own symbol and act as benign energy.

## Practical receivers against the bound

`wideband_receivers.cpp` computes the exact achievable rate (generalized mutual information) of quadratic FFT receivers in the large-alphabet limit with ideal interleaving and known timing and Doppler centre. Gaps are `10·log₁₀(R_bound/R)` at the same C/N₀: the extra transmit time, and energy, at the same transmit power. In the quadratic regime the equivalent C/N₀ penalty is about half that dB value ([Section 5 of the output](../../../_appraisal/algorithms-matrixModem/coherence-limits/results/wideband_receivers.md)). [Full output](../../../_appraisal/algorithms-matrixModem/coherence-limits/results/wideband_receivers.md).

**Long dwell plus spread-matched weighting reaches the bound.** The weighted periodogram computes fine-resolution bins over the dwell and weights each by `λ_k/(1 + λ_k)` from the expected signal energy in that bin; it is the long-observation form of the estimator-correlator, and Q65's fast-fading metric is a fielded instance ([practice review](mfsk-alternatives-practice-and-computation.md#fielded-weak-signal-mfsk-q65-as-the-benchmark)). Gap at −10 dB-Hz versus dwell:

| Channel | 0.16 s | 1.28 s | 10.24 s | 81.92 s |
| --- | ---: | ---: | ---: | ---: |
| Gaussian 0.1 Hz | 14.0 dB | 5.4 dB | 1.2 dB | 0.15 dB |
| Gaussian 1 Hz | 5.5 dB | 1.2 dB | 0.15 dB | 0.02 dB |
| Gaussian 10 Hz | 1.0 dB | 0.12 dB | 0.02 dB | 0.00 dB |
| Gaussian 30 Hz | 0.33 dB | 0.04 dB | 0.01 dB | 0.00 dB |
| Two modes 1 Hz at ±5 Hz | 6.5 dB | 1.3 dB | 0.15 dB | 0.02 dB |
| Stable phase | 21.0 dB | 12.3 dB | 5.1 dB | 1.4 dB |
| Rician `K` = 1, 1 Hz diffuse | 18.2 dB | 11.6 dB | 6.4 dB | 2.1 dB |

In the large-alphabet limit a longer dwell does not hurt, whatever the Doppler. For a stationary channel the exact estimator-correlator's rate increases monotonically toward the bound as the dwell grows, because the normalized log-determinant of a Toeplitz covariance is non-increasing in its size; the weighted periodogram follows it, with deviations of at most 0.1 dB in the tested cases. A dwell of about `10/(2σ)` brings the weighted-periodogram loss to about 0.15 dB. The exact estimator-correlator needs about half that dwell for the same loss; an independent finite-burst calculation gave 0.12–0.16 dB at `T·2σ = 5` ([burst-length table](#where-reality-is-worse-than-the-bound)). On stable and specular channels a per-symbol tone needs very long dwells to approach the coherent limit; this is the noncoherent cost of an unknown phase per symbol, `ln(1+E)/E` for symbol energy `E`.

**Simpler receivers.** At −10 dB-Hz:

| Channel | FT8-like (160 ms, one bin) | best segment-energy receiver | best single coherent bin | matched weighted periodogram |
| --- | ---: | ---: | ---: | ---: |
| Gaussian 0.1 Hz | 14.0 dB | 0.9 dB | 1.6 dB | 0.15 dB |
| Gaussian 1 Hz | 5.5 dB | 0.7 dB | 1.8 dB | 0.02 dB |
| Gaussian 10 Hz | 3.0 dB | 0.6 dB | 1.8 dB | 0.00 dB |
| Gaussian 30 Hz | 6.8 dB | 0.8 dB | 1.9 dB | 0.00 dB |
| Two modes 1 Hz at ±5 Hz | 8.3 dB | 3.7 dB | 4.8 dB | 0.02 dB |
| Auroral 50 Hz at +20 Hz | 8.8 dB | 0.8 dB | 2.0 dB | 0.00 dB |

The FT8-like detector's 14 dB gap on a 0.1 Hz channel means 25 times the transmit time, or about 7 dB of power. It is the cost of a fixed short symbol at low C/N₀, not of MFSK. A segment-energy receiver whose segment length and bin count suit the channel stays within about 1 dB of time, but that optimum moves from 40 s segments at 0.1 Hz to 0.3 s at 30 Hz, so it must adapt. These results agree with the earlier [Opus-5.5 Monte Carlo](../Opus-5.5/03-information-limits-and-noncoherent-penalties.md#best-symbol-duration-and-the-cost-of-fixed-durations), which found 13.9 dB for a fixed 160 ms symbol on the 0.1 Hz channel at −10 dB-Hz by a different method.

### Robustness to unknown spread

A weighted periodogram designed for one spread and used on another, at −10 dB-Hz and a 10.24 s dwell (time gap; the power-equivalent is about half):

| designed for \ actual | 0.1 Hz | 0.5 Hz | 1 Hz | 3 Hz | 10 Hz | 30 Hz |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.1 Hz | 1.2 | 3.1 | 5.4 | 9.8 | 14.8 | 19.6 |
| 1 Hz | 6.2 | 1.1 | 0.2 | 2.2 | 6.9 | 11.6 |
| 10 Hz | 15.6 | 9.7 | 6.9 | 2.6 | 0.0 | 2.2 |
| 30 Hz | 20.3 | 14.4 | 11.6 | 7.0 | 2.2 | 0.0 |

A factor of three error costs about 2.2 dB of time (1.1 dB of power) and a factor of ten 6–7 dB of time, consistent with the computation review's deflection figures after conversion. An error of 10 dB in the C/N₀ assumed for the weights costs under about 1.6 dB. A fixed spread design is therefore a drastic-failure risk, while a bank of spread hypotheses spaced by √2 to 2 costs at most about 0.1–0.5 dB of power. The bank's selection must be made over many symbols or by the decoder: the finite-alphabet Monte Carlo below shows that choosing the spread independently in each symbol is itself costly at low per-symbol SNR.

### Finite alphabet in a 2.2 kHz band

`finite_alphabet_mc.cpp` puts M = 11–352 tones in a 2.2 kHz band, generates each transmitted tone's fading signal by exact spectral synthesis, includes its leakage into neighboring tones, and estimates the per-symbol GMI by Monte Carlo with 10,000 trials per point (`±` about two standard errors, 0.05–0.3 dB here). It compares the adapted weighted periodogram (`WPm`, weights from the true channel, as a receiver that has learned the spread over the message), a per-symbol GLRT over six spread hypotheses, an unweighted band-energy detector (FT8-style when the dwell equals one over the spacing) and a single coherent bin. Gaps of the adapted receiver ([summary](../../../_appraisal/algorithms-matrixModem/coherence-limits/results/finite_alphabet_summary.md), [full output](../../../_appraisal/algorithms-matrixModem/coherence-limits/results/finite_alphabet_mc.md)):

| C/N₀ | spacing / dwell (M) | 0.1 Hz | 1 Hz | 10 Hz | 30 Hz | two modes ±5 Hz | worst of these | stable phase | Rician `K`=1 |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| −10 dB-Hz | 6.25 Hz / 41 s (352) | 0.31 | −0.05 | 0.94 | 6.31 | 0.52 | 6.31 | 2.24 | 3.24 |
| −10 dB-Hz | 25 Hz / 10.2 s (88) | 1.15 | 0.45 | −0.10 | 1.84 | 0.23 | 1.84 | 5.13 | 6.33 |
| −10 dB-Hz | 100 Hz / 41 s (22) | 1.07 | 0.27 | 0.23 | 0.32 | 0.33 | 1.07 | 3.48 | 3.84 |
| −10 dB-Hz | 200 Hz / 41 s (11) | 1.45 | 0.56 | 0.55 | 0.50 | 0.55 | 1.45 | 4.25 | 4.26 |
| 0 dB-Hz | 6.25 Hz / 2.56 s (352) | 1.88 | 0.58 | 0.17 | 0.86 | — | 1.88 | 2.97 | 3.02 |
| 0 dB-Hz | 25 Hz / 2.56 s (88) | 2.32 | 0.75 | 0.07 | 0.73 | — | 2.32 | 3.08 | 3.14 |
| 0 dB-Hz | 100 Hz / 2.56 s (22) | 2.97 | 1.04 | 0.27 | 0.14 | — | 2.97 | 3.63 | 3.34 |
| 0 dB-Hz | 100 Hz / 41 s (22) | 9.24 cap | 4.50 cap | 0.56 | 0.37 | — | 9.24 | 11.2 cap | — |
| +10 dB-Hz | 6.25 Hz / 0.16 s (352) | 3.72 | 2.39 | 0.91 | 0.36 | 3.71 | 3.72 | 3.90 | 2.91 |
| +10 dB-Hz | 6.25 Hz / 0.64 s (352) | 3.42 | 2.38 | 0.31 | 0.43 | 1.95 | 3.42 | 2.15 | 2.48 |
| +10 dB-Hz | 100 Hz / 0.64 s (22) | 5.45 | 4.08 | 1.12 | 0.46 | 3.37 | 5.45 | 4.04 | 4.38 |

“cap” marks rates limited by the alphabet: the receiver already extracts nearly `log₂M` bits per dwell, so the rate cannot exceed `log₂M/T`.

- **With an adapted receiver, finite alphabets stay close to the bound when the dwell suits the C/N₀.** At −10 dB-Hz the best configuration for each Doppler-spread channel is within about 0.3 dB of time, and the most uniform single configuration tested (100 Hz spacing, 41 s dwell) is within 1.1 dB on all of them. Slight negative entries are Monte Carlo error.
- **Spacing trades slow-channel efficiency against fast-channel safety.** A 6.25 Hz grid is best on a 0.1 Hz channel (0.31 dB) because it gives the largest alphabet, but at 30 Hz neighboring tones become confusable and the loss grows with dwell to 6.3 dB. A 100 Hz grid loses about 1 dB on slow channels and stays within 0.4 dB on fast ones; it has the smallest worst case in the −10 dB-Hz stratum.
- **Configuration errors are asymmetric.** A dwell that is too long for the actual C/N₀ only caps the rate (for example 9.2 dB of time at 0 dB-Hz with a 41 s dwell and 22 tones on a 0.1 Hz path): the message is slower than the channel allows but still decodes. A dwell that is too short for a weaker signal costs sensitivity and, at the extreme, can prevent decoding within any practical time. Under the stated preference for avoiding drastic failure, configure the dwell for the weakest C/N₀ the purpose must survive.
- **Choosing the spread per symbol is costly at low per-symbol SNR.** The per-symbol GLRT over six spread hypotheses loses a median 3.3–4.3 dB of time relative to the adapted receiver at −10 dB-Hz, about 8–11 dB on 10 Hz channels, and does not resolve at 30 Hz. At 0 dB-Hz the median excess falls to 0.2–1.2 dB, and at +10 dB-Hz to 0–0.7 dB. Spread selection should use many symbols or the decoder whenever the per-symbol energy is low.
- **Stable and specular channels expose the per-symbol noncoherent cost at finite alphabet size**: 2.2–4.3 dB from the coherent limit at −10 dB-Hz with 41 s dwells. A phase-tracking branch, not a different alphabet, is the remedy.
- **At +10 dB-Hz the band, not the channel, limits MFSK.** The best configurations are 2–4 dB of time from the infinite-bandwidth bound on slow channels: a finite alphabet in 2.2 kHz cannot carry the rate the bound allows when the energy per coherence interval is high. This is a throughput limit in the easy regime, where coherent or wider-band signaling would do better, not a sensitivity failure. Here the FT8 geometry (6.25 Hz, 160 ms) is as good as the adapted receiver allows on slow channels (3.7 dB at 0.1 Hz). On a 30 Hz channel the same grid with the adapted weighting stays within 0.4 dB, while its single-bin detector loses 6.9 dB.
- **The unweighted band-energy detector** (all bins of a tone's band summed equally) is not resolved at −10 dB-Hz for multi-second dwells. At 0 dB-Hz on the 6.25 Hz grid it costs about 10–12 dB on stable and 0.1 Hz channels and 3–5 dB on 1–10 Hz channels. Unweighted integration over a band much wider than the signal's Doppler occupancy throws away the benefit of the long dwell.

## Tone versus chirp and other spread signals

**Chirps resolve multipath and pay for it at low SNR.** After dechirping, a linear chirp that sweeps 2.2 kHz over a dwell `T` places a path of delay `τ` at a frequency offset `(2200/T)·τ`. A tone sees two paths as one fading process, since delay only rotates its phase. With equal-power Rayleigh paths, matched receivers for both, and the gap to the flat-fading bound:

| Channel (delay, `2σ`) | C/N₀ | dwell | tone | chirp | chirp − tone |
| --- | ---: | ---: | ---: | ---: | ---: |
| F.1487 quiet (0.5 ms, 0.1 Hz) | −10 dB-Hz | 10.24 s | 1.15 dB | 2.19 dB | +1.0 dB |
| disturbed (2 ms, 1 Hz) | −10 dB-Hz | 1.28 s | 1.17 dB | 4.07 dB | +2.9 dB |
| NVIS (7 ms, 1 Hz) | −10 dB-Hz | 10.24 s | 0.15 dB | 2.59 dB | +2.4 dB |
| low-latitude disturbed (6 ms, 10 Hz) | 0 dB-Hz | 1.28 s | 0.12 dB | 1.74 dB | +1.6 dB |
| high-latitude disturbed (7 ms, 30 Hz) | 0 dB-Hz | 0.16 s | 0.32 dB | 3.28 dB | +3.0 dB |

The chirp is worse in every case, by up to about 3 dB of time (1.3–1.5 dB of power), and the difference vanishes only when the dechirped paths fall within the Doppler spread. This is the low-SNR penalty for splitting energy across independently fading components: in the quadratic regime the rate from two independent halves is half the rate of the combined process. It complements, and does not contradict, the [chirp advantage reported by Marquet and coauthors](weak-signal-mfsk-alternatives.md#direct-comparisons) on a static frequency-selective channel at higher SNR without coding. A static notch erases a fixed tone; frequency hopping with coding across symbols recovers it, while the chirp's in-symbol diversity costs sensitivity on fading paths at weak signal levels.

**Keyed phase codes and DSSS.** In frequency-flat fading every constant-envelope signal has the same channel-learning cost, so a long keyed phase sequence per message hypothesis, detected by correlating and then measuring narrowband energy, has the same sensitivity as a tone; this was the mechanism of the historical single-carrier Robust Modem ([prior modems](prior-modems-and-other-paths.md#historical-single-carrier-robust-useful-mechanism-weak-continuity)). The difference is computation: each hypothesis needs its own correlation, while an FFT evaluates all tone hypotheses together. With delay spread, a wideband chip stream resolves paths and pays the same penalty as the chirp; a noise-like signal filling 2.2 kHz is overspread for weak HF ([Médard and Gallager, IEEE TIT 48(4), 2002](https://doi.org/10.1109/18.992769); [Telatar and Tse, IEEE TIT 46(4), 2000](https://doi.org/10.1109/18.850678); critical-bandwidth arithmetic in the [Opus-5.5 theory review](../Opus-5.5/reviews/review-noncoherent-theory.md)).

**Walsh orthogonal signaling** (STANAG 4415, 3G ALE) has the AWGN performance of MFSK of the same order, but its short phase-coherent symbols resolve multipath and need coherence over the symbol. **OFDM and OTFS** rely on channel estimation that the weak-signal regime cannot support without large pilot overhead; no low-SNR HF comparison was found.

**PSK families.** Constant-envelope PSK with recursive training achieves the same wideband limit `P − ∫ln(1+P·S)`, with interleaving, decision feedback and very short symbols [Zhang and Laneman, IEEE TIT 53(1), 2007, Theorem 4.1, full text](https://arxiv.org/abs/cs/0508121). So MFSK is not uniquely optimal; any constant-envelope signaling with an asymptotically optimal channel estimator reaches the bound. The power constraint decides what is needed. With an average-power constraint only, imperfect fading knowledge gives zero wideband slope and the optimum requires flash signaling, peaky in time or frequency [Verdú, IEEE TIT 48(6):1319–1343, 2002, abs](https://doi.org/10.1109/TIT.2002.1003824); [Telatar and Tse 2000](https://doi.org/10.1109/18.850678). A peak constraint caps that peakiness at the peak-to-average ratio, which is why the [transmitter-peakiness table](#where-the-real-channel-can-be-better) rewards bursting; when average equals peak, constant-envelope signaling at the peak, whether tones or PSK, achieves the bound. The practical question is how much memory a PSK receiver needs. In the low-SNR limit, joint detection of non-overlapping blocks of `N` symbols reaches the following fraction of the bound's rate ([`psk_lowsnr.py`](../../../_appraisal/algorithms-matrixModem/coherence-limits/results/psk_lowsnr.md)):

| Detector | fraction of bound | extra time | optimum symbol duration |
| --- | ---: | ---: | ---: |
| Differential PSK (N = 2) | 0.23 | 6.3 dB | `0.22/(2σ)` |
| Block, N = 8 | 0.60 | 2.2 dB | `0.11/(2σ)` |
| Block, N = 16 | 0.72 | 1.5 dB | `0.08/(2σ)` |
| Block, N = 64 | 0.86 | 0.7 dB | `0.04/(2σ)` |
| Tone, single coherent bin | 0.66 | 1.8 dB | `0.71/(2σ)` |
| Tone, weighted periodogram, long dwell | → 1 | → 0 | long |

Ordinary differential PSK loses about 3 dB of power-equivalent sensitivity in the quadratic regime and its optimum symbol rate is tied to the Doppler spread, so it must know the channel category. Long-memory PSK detection approaches the bound but needs iterative estimation and decoding over many symbols per hypothesis. PSK is the better choice where the channel is stable or strongly specular and its phase can be tracked: coherent BPSK with a long code reached about −1 dB `Eb/N₀` on phase-stable VLF ([empirical anchors](../Opus-5.5/04-empirical-weak-signal-anchors.md)), a regime in which a per-symbol noncoherent tone needs dwells of minutes to come within 1–2 dB. Phase-continuous FSK with multi-symbol coherent detection reaches the same regime without changing the transmitter.

## Receiver computation

Per symbol, the weighted periodogram costs one FFT share plus a weighted sum over the bins covering each tone's spread; a bank of spread hypotheses is a small matrix product of bin energies by weight vectors, which maps naturally onto batched matrix hardware. The other families are not cheaper as complete receivers: chirps need a dechirp and an FFT per symbol, keyed phase codes need one correlation per hypothesis, DSSS adds a Doppler-bin dimension that grows with coherent time, and long-memory PSK needs iterative estimation. The [practice and computation review](mfsk-alternatives-practice-and-computation.md#receiver-computation) gives the published numbers: a Taylor-tree drift search loses 1.4–2.5 dB where brute-force and FDMT searches do not, GPU FFTs are bandwidth-bound, tensor-core transforms pay off only when the chain is fused, and the sparse FFT saves about 2× only away from threshold. Across planetary entry links, GNSS and weak-signal amateur practice, the hypothesis count for timing, frequency, drift and spread dominates both the threshold and the cost; reducing it with Helper or GPSDO references is the main computational lever.

Three implementation losses belong in any comparison against the bound, because each can exceed the waveform differences discussed above ([details](mfsk-alternatives-practice-and-computation.md#acquisition-grids)):

- **Grid losses.** A rectangular-window frequency grid loses up to 3.92 dB at the worst offset, 0.91 dB with 2× zero-padding and about 0.3 dB with interbinning. A timing grid with window hops of `T/2`, `T/4` or `T/8` loses up to 2.5, 1.16 or 0.56 dB when the window is coherent, and about half as much with a long incoherent dwell.
- **Approximate drift search can fail drastically outside its design range.** The Taylor-tree dedoppler loses 1.4–2.5 dB within its unit drift rate, and beyond it its efficiency falls roughly in inverse proportion to the drift rate, to about 4% (−14 dB) at the edge of a ±4 Hz/s search with a 0.15 Hz/s unit rate. An exact search does not have this failure mode.
- **Low-precision arithmetic.** Four-bit input costs about 0.05 dB on clean noise but several dB when a narrowband interferer 20 dB above the noise is quantized with it, and a four-bit DFT matrix has an error floor of only −20 to −25 dB. The first is a property of the input, the second of the transform; both are acceptable only after channel isolation. The existing [tensor receiver results](tensor-receiver-bottlenecks.md) and [execution evidence](tensor-execution-evidence.md) cover what has been measured on the target hardware.

## Implications for Matrix Modem

These are design inferences from the calculations and literature. Each needs confirmation in a native receiver with acquisition, coding and physical completion. Better designs may exist.

1. **Keep phase-continuous, constant-envelope MFSK as the reference waveform, at the highest available peak power.** No family found is substantially better in sensitivity on Doppler-spread channels, and the alternatives that match it in theory need more receiver memory or more computation. Constant envelope at peak is optimal under a PEP limit.
2. **Configure the dwell from the weakest C/N₀ the purpose must survive, not from the Doppler category.** In the large-alphabet limit a longer dwell does not hurt on any Doppler spread, and short dwells at low C/N₀ cost up to 14–21 dB of time. In the 2.2 kHz Monte Carlo the best dwells put about 0–8 dB of energy in each symbol (`C/N₀·T` ≈ 1–6). A dwell that turns out too long for the actual signal only caps the rate; one that is too short costs sensitivity.
3. **With long dwells, make the tone spacing several times the widest Doppler spread the configuration must survive.** At −10 dB-Hz, 100 Hz spacing had the smallest worst case over 0.1–30 Hz spreads (1.1 dB of time), while a 6.25 Hz grid lost 6.3 dB at 30 Hz because adjacent tones became confusable. At short dwells and higher C/N₀ a fine grid with spread-aware weighting was acceptable (0.4 dB at 30 Hz). Use the Doppler category to set spacing and alphabet size, and keyed hopping across the band for frequency diversity against static notches and narrowband interference.
4. **Adapt the spread weighting per message, with a hypothesis bank spaced by √2–2, and include a narrow, long-coherence branch.** A fixed spread assumption risks losses of 6–20 dB of time. A per-message choice costs little; a per-symbol choice is costly at low SNR. A narrow branch with drift tracking captures specular or line-like power when present, which the Rician and quiet-path results show can be worth more than 10 dB. Its coherence depends on the equipment as well as the path: received mode widths near 0.06 Hz include equipment spread, which can exceed the ionospheric spread on one-hop paths, and a stable carrier has been seen to wander 0.2 Hz in 11 s, so the branch needs reference-oscillator stability and drift tracking of at least that order ([channel evidence](hf-channel-structure-and-extremes.md#coherence-on-quiet-paths)).
5. **Search drift exactly, and spend computation on hypotheses that matter.** A Taylor-tree approximation costs 1.4–2.5 dB within its design range and collapses beyond it; brute-force or FDMT-style search does not. Keep frequency and timing grids fine enough that their worst-case losses (up to 3.9 dB and 2.5 dB on coarse grids) stay below the margins being compared. Timing, frequency and drift hypotheses cost energy (about `log₂A` bits each message) as well as computation, so Helper or GPSDO narrowing is valuable for sensitivity as well as cost.
6. **Protect the energy detector.** Clip or blank impulses before the transform, use robust per-bin statistics, and treat bins with narrowband carriers as erasures. This addresses the one theoretical failure mode of MFSK energy detection identified here.
7. **Code across many fades and treat outages as erasures.** Interleave over at least the slowest fade and beat periods of interest, use a nonbinary code matched to the alphabet (coded modulation exceeds bit-interleaved capacity by about 1.9 dB at 64-FSK), and size deadlines with the finite-message table and the availability layer, not the ergodic bound. Focusing fades from traveling disturbances last ten minutes or more, so protection against the worst of them needs interleaving or time diversity spanning more than about 30 minutes. Frequency agility (higher frequencies during flare absorption, lower ones during storm depressions) is the main mitigation for outages, and it requires a configured schedule or band plan, because a one-way link has no feedback.
8. **Do not expect waveform changes or computation to recover the weak end of the link budget.** At −20 dB-Hz with a 1 Hz spread, the bound needs about a month per 100 bits. Peak power, antennas (including polarization diversity), lower noise, quieter or narrower paths, and specular modes are the levers that move a link out of the quadratic regime.

**When this conclusion could be wrong.** A strongly specular or very stable channel favors phase-tracking receivers, possibly with PSK. Very short messages on slow channels are dominated by outage, where in-symbol diversity (chirps, multitone) can help. Severe narrowband interference or impulsive noise may favor spreading with robust detection. A measured scattering function very different from the models here could change the numbers. None of these reverses the conclusion that a properly adapted MFSK receiver is a sound general-purpose reference.

## Distinctions to keep in view

Several conclusions above change meaning if two similar-sounding quantities are confused. These are the distinctions that mattered in this pass.

- **Proven, derived and heuristic.** The bound and its firm discrete-time version are theorems. That Gaussian fading is the worst fading at low SNR is a derivation from the Koch–Lapidoth identity plus Zhang–Mitra achievability, not a published theorem. The finite-message table is a random-coding heuristic. That Gaussian noise is the worst noise is not proven for this channel. Values marked [derived] in the companion reviews are reviewers' calculations. The nontrivial ones are reproducible from [`literature-checks/`](../../../_appraisal/algorithms-matrixModem/coherence-limits/README.md#literature-check-calculations); the rest are unit conversions or arithmetic stated where they appear.
- **Minimum power versus minimum time.** The bound is positive at every C/N₀. In this study, "cannot carry a short message" means 100 bits cannot fit within one diurnal cycle on one frequency ([definition](#how-hard-the-limit-is-in-hf-terms)). More than a day is acceptable for Matrix Modem. Longer airtime is an availability and receiver-memory question, not an impossibility. Recognizing that a keyed pattern is present uses the same evidence rate as decoding, needing about 18 nat against 69 nat for 100 bits, so it is not a way around the quadratic regime. Knowing the key removes uncertainty about where the tone is, not about the channel phase.
- **Ergodic rate versus outage.** Line-like and two-ray channels can be favorable for the ergodic bound and worse than Rayleigh in outage. Short messages and slow channels are judged by outage, not by the bound.
- **Time gap versus power gap.** Receiver gaps here are extra transmit time at equal power. In the quadratic regime the equivalent power penalty is about half as many dB. Deflection losses quoted in the computation review are power losses. Compare like with like.
- **Energy per coherence interval, per symbol and per branch.** `x = (C/N₀)/B_eff` is set by power and channel. Energy per symbol, `C/N₀·T`, is a configuration choice; in the 2.2 kHz Monte Carlo the best values were about 1–6. Energy per independently fading branch within a long dwell is at most about `C/N₀` times the coherence time, which no choice of symbol length can raise. Kennedy's optimum implicit diversity for binary orthogonal signaling, a mean SNR of about 3 per branch, is therefore reachable only when `x` is roughly 5 dB or more. At weak signals every branch is weak, and the quadratic penalty cannot be avoided by shortening symbols [inference from the cited results]. The equal-weight branch model used for Q65 ([practice review](mfsk-alternatives-practice-and-computation.md#fielded-weak-signal-mfsk-q65-as-the-benchmark)) describes a fixed alphabet and fixed bits per symbol. It does not contradict the large-alphabet result that longer dwells do not hurt.
- **Hypothesis-grid density versus selection timescale.** A spread-hypothesis grid spaced by √2 to 2 costs only 0.13–0.5 dB, so dense grids are unnecessary. Choosing among the hypotheses symbol by symbol at low per-symbol energy costs 3–11 dB of time, so the choice must be made over the message. Deflection figures omit the look-elsewhere cost of selection, and deflection is not detection probability at very low false-alarm rates.
- **Source asynchrony versus clock and frequency uncertainty.** The `log₂A` energy cost is for an unknown start time. Frequency, drift and spread uncertainty add their own hypotheses and costs.
- **Noise power versus noise distribution.** A fixed noise power with an impulsive distribution is expected to help the information limit and can still collapse an energy detector. Margins also depend on decile and site variation of about 10 dB, not on medians.
- **Input quantization, transform quantization and interference.** Quantizing Gaussian input, quantizing the DFT matrix and quantizing a strong interferer together with the signal are three separate losses with different magnitudes. An INT8 matrix contraction is not an INT8 FFT.
- **Equipment spread versus ionospheric spread.** On quiet one-hop paths, measured widths near 0.06 Hz include the equipment, which can dominate. The long-coherence branch is limited by the station's reference as well as by the ionosphere.
- **Width conventions.** Half-power, −10 dB, 80%-power and `2σ` widths differ greatly for heavy-tailed spectra. Only `B_eff` enters the bound; Jakes-type spectra have no finite `B_eff` at all.
- **Peak versus average power, and receive versus transmit antennas.** The bound is set by received peak C/N₀. An average-power constraint alone calls for flash signaling; a peak constraint caps the peakiness, and with average equal to peak, constant envelope at the peak achieves the bound. Extra receive branches always help. Extra transmit antennas help only if each brings its own amplifier power.
- **Configuration errors that cap throughput versus errors that break reception.** A dwell that is too long, or a spread hypothesis range that is too wide, costs rate or a fraction of a dB. A dwell that is too short for the actual signal, a single fixed spread assumption, tone spacing narrower than the Doppler occupancy, or an approximate drift search used beyond its design range can lose 6–20 dB or more. The design preference for avoiding drastic failure selects the first kind of error.
- **Stationary channel versus availability.** The bound describes stationary fading within minutes. Flare absorption, polar cap absorption and storm closures are outages measured in minutes to days, which belong to coding, deadlines and frequency planning.
- **Literature conditions versus Matrix Modem conditions.** Q65's a priori decoding gains need a small plausible message set and do not apply to an encrypted payload. Turbo-FSK's gain needs phase coherence across repetitions. The NVIS study that ranked FSK last used uncoded 0.5 ms symbols at high SNR. Each result is valid in its own setting and transfers only where its conditions hold.

## Reconciliation with earlier repository findings

- **Opus-5.5 information limits.** Its table of required `Eb/N₀`, its statement that energy per coherence cell governs reception, and its 13.9 dB fixed-160 ms penalty at 0.1 Hz and −10 dB-Hz are reproduced here by an independent closed-form method. Its claim that no waveform family can do materially better in the low-C/N₀ regime is supported, with the qualifications above: specular components, receive branches, bursting above average power, and the finite-message and nonstationary effects. [Opus-5.5 chapter 3](../Opus-5.5/03-information-limits-and-noncoherent-penalties.md).
- **Sol-6.2 information-limit audit.** Its conclusion that the expression is a conditional asymptotic capacity is confirmed and extended: Gaussian fading is the worst fading for a given autocorrelation in the capacity-per-energy sense, the Gaussian shape is within 0.2 dB of the worst for a given RMS width, and widths must be converted through `B_eff`. [Information limits under Doppler fading](information-limits-review.md).
- **Opus-5.5 “in-symbol diversity hurts the coded rate.”** Consistent with the chirp-versus-tone and PSK-block results: splitting energy into independently fading pieces costs rate in the quadratic regime.
- **Earlier chirp evidence.** The [direct comparisons](weak-signal-mfsk-alternatives.md#direct-comparisons) favoring chirps on static frequency-selective channels stand; the new result adds that on Rayleigh multipath at low SNR the chirp is worse by up to about 1.5 dB of power.
- **Earlier PSK statement.** The [alternatives review](weak-signal-mfsk-alternatives.md#receiver-computation-and-matrix-modem) placed DBPSK and 8-FSK within 0.1 dB in slow flat Rayleigh at high SNR. In the weak, time-varying regime differential PSK is much weaker (23% of the bound's rate), and only long-memory PSK detection is competitive.
- **Earlier 6.25 Hz default.** The FT8-like configuration is a useful reference, but the results here show it is a high-C/N₀ operating point: at −10 dB-Hz it needs 5.5–14 dB more time than the bound on 0.1–1 Hz channels and loses through leakage at 30 Hz.
- **Opus-5.5 channel-aware airtimes.** Its best-symbol MFSK times for 128 bits, for example 9.1 h at −10 dB-Hz and 1 Hz, agree within about 3% with the bound's 100-bit times scaled by 128/100 ([Opus-5.5 chapter 2](../Opus-5.5/02-noise-and-link-budget.md#channel-aware-airtime)). The [scenario table](#plausible-transmit-time-at-2100-mw-and-150-db-or-more) adds a practical-receiver and availability allowance. With them, Opus-5.5's "hours for short messages in quiet and moderate conditions" at 100 mW and 170 dB becomes 2–4 hours on quiet paths and about a day on moderate ones. Its statement that short messages below about −17 dB-Hz take days even on quiet paths agrees: 2.9 days at 0.1 Hz.

## Open questions and limits of this study

- The large-alphabet results assume known timing and Doppler centre, ideal interleaving and stationary Gaussian statistics. Acquisition, drift tracking, a real code and physical completion are not modeled.
- The finite-message table is a random-coding heuristic. A low-SNR finite-blocklength theory for stationary noncoherent fading does not yet exist.
- The worst-case-fading argument is asymptotic. Outage on slow channels with two comparable rays is worse than Rayleigh and needs measured traces.
- Measured HF scattering functions with sub-0.1 Hz resolution, specular fractions, and their occurrence statistics are scarce in accessible literature. They decide how much a long-coherence branch is worth.
- Impulsive-noise robustness of the energy detector, and the cost of the nonlinear front end, need measurement on recorded HF noise.
- No 2018–2026 head-to-head of MFSK against chirp, OTFS or DSSS on Watterson channels at low SNR was found.
- Multi-day transmit times are estimated from a stationary bound with an assumed availability fraction. Several parts of that estimate are not modeled or measured. They are a time-varying channel model, availability on real paths, and the upper deciles of day-to-day signal level. The size of the strong-period advantage is also unknown, and so is the receiver that would collect it. That receiver would weight evidence by segment and hold timing, frequency and evidence for days.
- Whether Helper Carrier tones share the data tones' fading closely enough to serve as a channel reference has not been measured. The appraisal currently keeps Helper separate from data detection.
- No capacity result for nonstationary Doppler spectra, no proof that Gaussian noise is worst for the peak-limited noncoherent fading channel, and no universal-decoding theorem for continuous stationary noncoherent fading were found.
- Sources not accessed in full: Feder and Lapidoth 1998, Ligong Wang's master's thesis on spectral lines, Bergel and Benedetto, Chowdhury et al., Kowshik and Polyanskiy, Lancho et al., Qi and Koch, Gursoy–Poor–Verdú Part I, Tchamkerten–Chandar–Wornell 2009 (used through Polyanskiy), Rao and Hassibi, Wu and Srikant, Spaulding and Middleton, and the Prelov and Pinsker weak-signal papers; Diggavi and Cover was only skimmed.

**Recommended next experiment.** Implement the adaptive weighted-periodogram receiver in the native Matrix stack and test it with acquisition, drift, a real code, physical completion and impulsive noise included, against the large-alphabet and 2.2 kHz results here.

## Primary sources

Citations in this study and its two companion reviews were checked against Crossref, arXiv and the publishers' pages on 25 November 2026. Eleven DOIs and URLs that had been built from citation patterns were wrong and were corrected; one amateur article with no stable link (Egano 2017) is cited without one.

- V. Sethuraman and B. Hajek, “Capacity per unit energy of fading channels with a peak constraint,” *IEEE Trans. Inf. Theory* 51(9), 2005. [Author PDF](https://hajek.ece.illinois.edu/Papers/SethuramanHajek05.pdf)
- V. Sethuraman, L. Wang, B. Hajek and A. Lapidoth, “Low SNR capacity of noncoherent fading channels,” *IEEE Trans. Inf. Theory* 55(4), 2009. [arXiv 0712.2872](https://arxiv.org/abs/0712.2872)
- T. Koch and A. Lapidoth, “Gaussian fading is the worst fading,” *IEEE Trans. Inf. Theory* 56(3), 2010. [arXiv 0902.3372](https://arxiv.org/abs/0902.3372)
- A. Lapidoth and L. Wang, “On the low SNR capacity of peak-limited non-coherent fading channels with memory.” [arXiv cs/0604031](https://arxiv.org/abs/cs/0604031)
- M. C. Gursoy, H. V. Poor and S. Verdú, “The noncoherent Rician fading channel — Part II,” *IEEE Trans. Wireless Commun.* 4(5), 2005. [arXiv cs/0501067](https://arxiv.org/abs/cs/0501067)
- W. Zhang and J. N. Laneman, “How good is PSK for peak-limited fading channels in the low-SNR regime?” *IEEE Trans. Inf. Theory* 53(1), 2007. [arXiv cs/0508121](https://arxiv.org/abs/cs/0508121)
- W. Zhang and U. Mitra, “Orthogonal codes for robust low-cost communication.” [arXiv cs/0702108](https://arxiv.org/abs/cs/0702108)
- U. G. Schuster, G. Durisi, H. Bölcskei and H. V. Poor, “Capacity bounds for peak-constrained multiantenna wideband channels.” [arXiv 0801.1002](https://arxiv.org/abs/0801.1002)
- V. Chandar, A. Tchamkerten and D. Tse, “Asynchronous capacity per unit cost,” *IEEE Trans. Inf. Theory* 59(3), 2013. [arXiv 1007.4872](https://arxiv.org/abs/1007.4872)
- Y. Polyanskiy, “Asynchronous communication: exact synchronization, universality and dispersion,” *IEEE Trans. Inf. Theory* 59(3), 2013. [Author PDF](https://people.lids.mit.edu/yp/homepage/data/async.pdf)
- A. J. Viterbi, “Performance of an M-ary orthogonal communication system using stationary stochastic signals,” *IEEE Trans. Inf. Theory* 13(3), 1967. [DOI](https://doi.org/10.1109/TIT.1967.1054039)
- G. Durisi, H. Bölcskei and S. Shamai, “Capacity of underspread noncoherent WSSUS fading channels under peak signal constraints,” ISIT 2007. [arXiv 0705.1682](https://arxiv.org/abs/0705.1682)
- M. Médard and R. G. Gallager, “Bandwidth scaling for fading multipath channels,” *IEEE Trans. Inf. Theory* 48(4), 2002. [DOI](https://doi.org/10.1109/18.992769)
- I. E. Telatar and D. N. C. Tse, “Capacity and mutual information of wideband multipath fading channels,” *IEEE Trans. Inf. Theory* 46(4), 2000. [DOI](https://doi.org/10.1109/18.850678)
- M. Feder and A. Lapidoth, “Universal decoding for channels with memory,” *IEEE Trans. Inf. Theory* 44(5), 1998. [DOI](https://doi.org/10.1109/18.705540)
- Z. Sun, S. M. Perlaza, D. Mary and J.-M. Gorce, finite-blocklength information density for Gauss–Markov fading, ISIT 2026. [arXiv 2604.16062](https://arxiv.org/abs/2604.16062)
- S. Verdú, “Spectral efficiency in the wideband regime,” *IEEE Trans. Inf. Theory* 48(6), 2002. [DOI](https://doi.org/10.1109/TIT.2002.1003824)
- S. N. Diggavi and T. M. Cover, “The worst additive noise under a covariance constraint,” *IEEE Trans. Inf. Theory* 47(7), 2001. [DOI](https://doi.org/10.1109/18.959289)
- A. Lancho, T. Koch and G. Durisi, “On single-antenna Rayleigh block-fading channels at finite blocklength,” *IEEE Trans. Inf. Theory* 66(1), 2020. [DOI](https://doi.org/10.1109/TIT.2019.2945782)

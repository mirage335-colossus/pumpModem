# Over-the-horizon radar and interference tolerance of the FSK lattice receiver

26 November 2026 · Fable-5.2 pass · simulation of model radars, with the literature review

The requirement is that the configured SNR path-loss margin and the interleaving keep the link
working despite over-the-horizon radar (OTHR). This note combines the
[OTHR literature review](reviews/review-othr-interference.md) with a time-domain simulation of the
lattice receiver,
[`othr_tolerance.cpp`](../../../_appraisal/algorithms-matrixModem/othr-interference/README.md)
([output](../../../_appraisal/algorithms-matrixModem/othr-interference/results/othr_tolerance.md)).
The radars are models built from published and monitored parameters; no recording of a real radar
was used, and no occupancy probability exists for any amateur band.

## Answer in brief

- **Margin alone cannot be the defence.** An unhandled radar raises the noise by
  `10·log₁₀(1 + ρ·W/B)`, where `ρ` is how far the radar's in-band pulse stands above the passband
  noise, `W` the passband and `B` the sweep. A 10 kHz radar 25.5 dB above the noise uses up the
  20 dB option; radars are logged at 23–66 dB above it. A naive energy receiver lost 1 dB of margin
  per dB of radar in the simulation.
- **Interleaving alone cannot be the defence either.** Radars are logged as persistent for hours,
  and all of the modem's tone positions are inside the sweep, so hopping within the passband gives
  no diversity against it.
- **A conditioning stage ahead of the 4-bit sample code is what works.** With comb excision on a
  1.28 s low-sidelobe analysis, inverse-power sample weighting, an erasure code for saturated
  samples, per-position noise estimates from medians and clipped 4-bit evidence, the simulated
  margin consumed stayed at or below 1.8 dB for every coherent radar from 0 to 60 dB above the
  noise, with in-band duty from 7% to 70%, taking the better of the comb and no-comb chains.
- **What remains exposed** is a strong radar whose sweeps are not coherent at the receiver (a
  disturbed radar path or a random phase per sweep). Without a comb, the simulated loss was 2.7 dB
  at 10 dB above the noise, 3.1 dB at 20, 5.7 dB at 30, 10.8 dB at 40 and 23 dB at 60. Time gating
  cannot remove the ringing tails that a steep receive filter gives each crossing.
- **A limiter is the wrong front end.** Saturating 4-bit samples lost 3–8 dB at 20–30 dB above the
  noise for coherent radars, more for incoherent ones, and failed at 60 dB. Erasing saturated samples instead is better but still fails at very
  high levels without the weighting and excision stages.
- **Budget:** allow 1–2 dB of margin for one coherent radar with conditioning, 3–4 dB for the
  harder documented cases, and treat a strong incoherent radar as not covered. The 3 dB margin
  option can afford the first and nothing else.

## What the radar looks like to the modem

From the review ([§1](reviews/review-othr-interference.md)): most documented OTHR in and near the
amateur bands is a periodic linear-FM sweep 10–160 kHz wide repeating 7–83 times a second.
Monitoring gives 12 kHz at 40 sweeps/s for the Russian 29B6, 10 kHz at 42–83 sweeps/s in 2.5–10 s
bursts for the Chinese burst radar, 20–80 kHz at 12.5–50 sweeps/s for the Cyprus radar, and
22–300 kHz once every 0.13–1 s for oceanographic radars. These are volunteer logs and ITU
documents; levels are S-meter readings.

In a 2.8 kHz passband such a radar is three things at once:

- **A pulse train in time,** one crossing per sweep, with duty `W/B`: 28% for 10 kHz, 14% for
  20 kHz, 7% for 40 kHz. Each crossing has a small time-bandwidth product (about 16 for 10 kHz at
  50 sweeps/s), so a steep filter leaves long tails: removing 99% of the energy of that radar needs
  39% of the samples and 99.9% needs 59%.
- **A comb in frequency,** one line every sweep-repetition frequency, when the sweeps are coherent,
  which the radar's own Doppler processing requires within a dwell of 0.7–40 s. A 4 Hz notch
  removes 8% of the spectrum at 50 sweeps/s and left −48 dB with 1 Hz of Doppler spread on the
  radar's path; it does nothing at 10 Hz of spread or with random sweep phase.
- **A noise-floor rise at the modem's own resolution.** Every 6.25 Hz by 160 ms cell is touched,
  and with a rectangular window no bin is more than 10 dB below the mean. Nothing is sparse there,
  so excision has to happen before the symbol transform, on a longer and better-windowed analysis.

Radars choose channels that look clear, and a modem below the noise looks clear, so the radar will
not avoid it.

## Simulation

One scene is 96 consecutive 0.4 s dwells sharing one radar. The radar is generated at 12.8 kHz,
passed through a steep 2.8 kHz filter (255-tap Kaiser) and decimated to 3,200 Hz, so crossings have
realistic tails. The comb sits at a random offset from the tone grid. Each dwell carries a wanted
tone at −6 dB per dwell on a fresh keyed random position. The result is the weak-signal deflection
of the 4-bit evidence relative to an unquantized receiver with no radar, expressed as
received-power margin consumed. 160 scenes per point; the scatter is about ±0.2 dB.

Receiver chains:

| Chain | Front end | Noise estimate | Evidence |
| --- | --- | --- | --- |
| naive | linear samples | one global value | unquantized energy |
| per-bin | linear samples | per-position median over the scene | 4-bit, clipped |
| limiter | 16-level samples scaled to the noise floor, saturating | per-position | 4-bit |
| erase | 15-level samples plus an erasure code for any sample beyond 3.4σ | per-position | 4-bit |
| weight + erase | inverse-power weighting over 2.5 ms, then the erasure code | per-position | 4-bit |
| comb + weight + erase | 4,096-point Blackman–Harris analysis and synthesis at 75% overlap, bins whose median exceeds six times the lower quartile zeroed with two neighbours; then weight + erase | per-position | 4-bit |

The noise scale comes from the lower quartile of the sample magnitudes, which a radar duty below
about 70% does not move.

**Margin consumed (dB), by radar level above the passband noise**

| Radar (in-band duty) | Chain | none | 0 dB | 10 dB | 20 dB | 30 dB | 40 dB | 60 dB |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 12 kHz at 40/s, coherent (23%) | naive | 0.0 | 1.4 | 9.1 | 18.4 | 28.2 | >30 | >30 |
| | per-bin | 0.1 | 0.3 | 1.1 | 3.6 | 13.5 | >30 | >30 |
| | limiter | 0.0 | 0.4 | 1.3 | 3.0 | 4.9 | 10.3 | >30 |
| | erase | 0.1 | 0.5 | 3.8 | 3.1 | 4.2 | 9.6 | >30 |
| | weight + erase | 0.1 | 0.5 | 0.8 | 1.9 | 2.9 | 5.9 | 19.0 |
| | comb + weight + erase | 0.1 | 0.4 | 0.5 | 0.8 | 0.9 | 1.0 | 1.3 |
| 10 kHz at 50/s, coherent (28%) | weight + erase | 0.3 | 0.6 | 1.1 | 2.2 | 3.7 | 6.6 | 25.5 |
| | comb + weight + erase | 0.3 | 0.4 | 0.6 | 0.7 | 0.9 | 0.7 | 0.5 |
| 10 kHz at 66.67/s, coherent (28%) | weight + erase | 0.2 | 0.5 | 1.2 | 2.2 | 4.3 | 8.8 | 29.4 |
| | comb + weight + erase | 0.2 | 0.4 | 0.5 | 0.5 | 0.8 | 0.6 | 0.4 |
| 20 kHz at 25/s, coherent (14%) | weight + erase | 0.3 | 0.3 | 0.6 | 1.0 | 1.9 | 3.1 | 5.8 |
| | comb + weight + erase | 0.3 | 0.3 | 0.7 | 1.0 | 1.4 | 1.4 | 1.5 |
| 40 kHz at 10/s, coherent (7%) | weight + erase | 0.2 | 0.2 | 0.4 | 0.7 | 0.9 | 1.4 | 1.8 |
| | comb + weight + erase | 0.2 | 0.2 | 1.6 | 2.9 | 3.5 | 4.2 | 4.7 |
| 8 kHz at 32/s, coherent (35%) | weight + erase | 0.2 | 0.8 | 1.6 | 2.4 | 4.0 | 5.8 | 19.1 |
| | comb + weight + erase | 0.2 | 0.9 | 1.1 | 1.0 | 0.9 | 1.0 | 1.2 |
| 4 kHz at 25/s, coherent (70%) | weight + erase | 0.0 | 0.8 | 2.3 | 6.4 | 13.4 | >30 | >30 |
| | comb + weight + erase | 0.0 | 0.8 | 0.8 | 1.3 | 0.9 | 1.5 | 1.5 |
| 25.6 kHz at 1/s, coherent, ungated (11%) | weight + erase | 0.4 | 0.7 | 0.7 | 0.7 | 0.7 | 1.0 | 0.8 |
| | comb + weight + erase | 0.4 | 0.7 | 0.7 | 0.7 | 0.7 | 1.0 | 0.8 |
| 10 kHz at 50/s, random phase per sweep (28%) | naive | 0.0 | 1.2 | 5.6 | 15.0 | 26.4 | >30 | >30 |
| | limiter | 0.0 | 1.3 | 5.3 | 10.4 | 15.5 | 18.8 | 25.8 |
| | weight + erase | 0.1 | 1.4 | 2.7 | 3.1 | 5.7 | 10.8 | 22.8 |
| | comb + weight + erase | 0.1 | 1.4 | 2.7 | 3.1 | 5.7 | 10.8 | 22.7 |

The full output has all six chains for every radar, with the fraction of tone positions notched
(up to 36% for the comb radars) and of samples erased.

What the table shows:

- **Every simpler chain fails without bound somewhere.** The naive receiver fails everywhere.
  Per-position normalisation alone fails once comb leakage through the rectangular symbol window
  fills the bins (30 dB and up). The limiter and the erasure code alone fail at 40–60 dB, because
  the filter tails of each crossing are neither clipped nor erased.
- **Comb excision is what bounds the loss for coherent radars,** at 0.4–1.5 dB from 0 to 60 dB
  above the noise, for duties from 14% to 70% where the lines are at least 25 Hz apart. The price
  is the notched positions, 8–36% of them, which the keyed placement turns into evenly spread lost
  dwells.
- **The comb chain must not be applied blindly.** For the 40 kHz radar at 10 sweeps/s the lines are
  only 10 Hz apart and the notches remove 42–85% of the positions; the no-comb chain is better
  there (0.2–1.8 dB). The receiver has to choose from measured line spacing and duty, as the review
  recommends, or run both and keep the better.
- **Taking the better of the two chains gives at most 1.8 dB for every coherent radar tested.**
- **With no radar present the full chain costs 0.0–0.4 dB,** inside the scatter of the run.
- **The incoherent radar is not bounded.** It has no comb, and gating leaves the tails. The loss is
  small only while the radar is within about 20 dB of the noise.
- **An erasure code alone has a bad corner just below its own threshold** (10 dB above the noise:
  3.8–6.5 dB, more than at 20 dB). It is the classic result that a pulsed interferer does
  disproportionate damage when it sits under the clip level. Inverse-power weighting removes that
  corner.

## What the literature adds

From the review ([§4–§6](reviews/review-othr-interference.md)) and the coding review
([§5](reviews/review-coding-interleaving.md)):

- **Summing raw energies under partial interference is the known wrong receiver.** With no side
  information, adding more hops per bit made worst-case partial-band jamming worse for a linear
  combiner (Lee, French and Miller 1984). In the coding review's bank Monte Carlo, 1% of cells hit
  at +20 dB takes a design with 6 dB of margin from no observed errors to an 80% error rate.
- **Normalise, then clip.** Dividing each dwell by the median of its own positions handles a hit on
  the whole dwell; clipping handles a hit on single positions; both together need no knowledge of
  the quiet noise level. M-ary signalling supplies the side information itself, because every
  dwell has many noise-only positions.
- **Erasure with knowledge costs little; a clipped metric without it costs more.** For 25% of cells
  hit hard, erasure with perfect knowledge costs 1.25 dB of time and a clipped metric 4.6 dB. A
  comb that stays put for minutes lets the receiver learn the per-position levels from hundreds of
  dwells and approach the first figure.
- **A regular interleaver has exact resonances.** For a codeword with regularly spaced symbols,
  about 1–4% of spacings lose at least 90% of the codeword to a burst radar or a 1 Hz oceanographic
  sweep at the worst phase, while the median spacing looks harmless. Pseudo-random placement stays
  near the mean hit fraction.
- **Margin and erased fraction are different resources.** A margin of X dB tolerates the textbook
  fraction `1 − 10^(−X/10)` only when the design has many weak looks. With the energy-optimal 18
  strong branches, 10 dB tolerates 62% random erasures, not 90%. The lattice's weak-signal
  operating point, many dwells near or below 0 dB, is the tolerant case. The coding review's
  figure for dwells near 0 dB: 0.57, 0.77, 0.87, 0.90, 0.91 and 0.92 of the dwells may be erased at
  random for margins of 3, 6, 10, 13, 16 and 20 dB.
- **Receiver setup.** AGC off or held, transceiver noise blanker and noise reduction off, widest
  linear filter, the noise 20–30 dB above the quantization floor of at least a 16-bit path. A
  transceiver's own blanker sees nothing to blank, because ahead of the SSB filter an FMCW radar
  is a constant-envelope carrier. A radar 51–66 dB above the noise is at or beyond the headroom of
  a 16-bit audio path set that way.

## Consequences for the specification

1. **Conditioning before the 4-bit code, at higher precision:** fixed gain; scale from a lower
   quantile; comb and carrier excision on at least one second of low-sidelobe analysis when a
   sparse comb is measured; inverse-power sample weighting; erasure of saturated samples.
2. **Two-way normalisation of the evidence:** per position over time and per dwell over positions,
   both by order statistics, then a clip. A position whose noise estimate is far below the floor
   after excision counts as erased.
3. **Keyed, uniform, aperiodic placement** of tones in frequency and of dwells in time, with no
   structure at 2, 4, 8 or 16 bins or at any fixed period.
4. **Block span at least ten times the longest burst to be bridged;** a persistent radar is handled
   by excision, not by the interleaver.
5. **A margin line for OTHR:** 1–2 dB for one coherent radar with conditioning; 3–4 dB for
   the harder documented cases (several radars, 7–10 sweeps/s at 10 kHz, blanking-only cases up to
   about 20 dB above the noise); not covered for a strong incoherent radar or when conditioning is
   off.
6. **Acceptance tests at 10, 30 and 60 dB above the passband noise,** coherent and incoherent,
   because each level exercises a different failure: the margin, the metric, and the excision depth
   and sample path.
7. **Adding dwells must never raise the error probability under interference.** This is the test
   for the linear-combining failure.

## Weaknesses and open items

- **Strong incoherent sweeps.** The principled remedy is excision in the dechirped domain, where
  each crossing collapses to a narrow cell and about `2·f_r/W` of the plane is removed (3.6% at
  50 sweeps/s) whether or not sweeps are coherent. It needs the sweep rate and timing, which a
  strong radar supplies. It was not simulated.
- **Real radars were not analysed.** Line width, phase noise, dwell boundaries, rate changes
  between bursts and the level statistics are unknown. The comb argument rests on the radar's need
  for coherent dwells.
- **Several radars at once** (up to five are logged) were not simulated.
- **Only the 0.4 s dwell was simulated.** A longer dwell resolves the comb at its own resolution,
  which should help, and needs its own run.
- **The excision here is simple:** a fixed threshold and neighbour count, one window, medians over
  the whole 38 s scene. Bursts shorter than the scene, a moving radar and a radar arriving or
  leaving were not tested. The first quarter-second after a change is not protected.
- **The deflection measure is a weak-signal quantity.** It is not a coded error rate, and it does
  not show what the notches and erasures do to a strong wanted signal.
- **Future radar waveforms may not be linear FM.** Constant-modulus periodic FSK, Costas and
  noise-like waveforms have been proposed. They would keep the comb and remove the time sparsity.
- **Other interferers** (broadcast carriers, digital signals, impulsive noise) were not simulated
  in this pass. Steady carriers are the easy case for the comb stage; the earlier
  [fading-limit study](../Sol-6.2/fading-limit-and-mfsk-alternatives.md#where-reality-is-worse-than-the-bound)
  covers why impulsive noise needs the same bounded-influence metric.

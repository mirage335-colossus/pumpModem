# FSK lattice design: dwell, alphabet, four-bit evidence and block size

**Later correction.** The subsequent [finite-window checks](../fsk-lattice-validity.md) and [presence study](../fsk-presence-calibration.md) qualify the timing, narrow-band diversity and threshold assumptions used below. The [receiver review](../fsk-reconciliation.md) connects these checks to measured block decisions.

26 November 2026, extended 27 November · Fable-5.2 pass · calculations and Monte Carlo, with reconciliation

This note gives the reasoning behind the waveform choices in the
[FSK specification](../../../_appraisal/matrixModem-FSK.md) and its [dated design study](../fsk-design-study.md). The numbers come from
[`lattice_receiver.cpp`](../../../_appraisal/algorithms-matrixModem/fsk-lattice/README.md)
([full output](../../../_appraisal/algorithms-matrixModem/fsk-lattice/results/lattice_receiver.md)),
which reuses the method of the earlier
[coherence-limit calculations](../../../_appraisal/algorithms-matrixModem/coherence-limits/README.md),
and from the four literature reviews of this pass. They are theoretical calculations and model
simulations: known timing, Gaussian noise, no interference, ideal coding unless a section says
otherwise. No receiver exists yet.

**Terms.** This note was written while "dwell" meant one tone held for a whole FHSS hop. The
specification now separates the two and no longer uses the word: a *tone* is one tone on one
position, a *hop* is the radio's stay on one hop frequency, and a *held tone* is a tone as long
as its hop. Read "dwell" below as a held tone, and "dwells per block" as tones per block. The
sections written after the change say tone and hop.

"Gap" always means extra transmit time at the same power relative to the peak-limited fading bound
of the [fading-limit study](../Sol-6.2/fading-limit-and-mfsk-alternatives.md). At weak signal
levels the equivalent power is about half as many dB.

## Answer in brief

- **Keep FT8's cell as the lattice, and build everything from whole cells.** 3,200 complex samples
  per second, 512 samples (160 ms) per cell, 6.25 Hz per bin, 352 tone positions in 2.2 kHz. Every
  duration is a whole number of cells and every tone sits on a bin. That makes one FFT plan, exact
  orthogonality, and an exact cell-domain simulation.
- **The dwell, not the cell, decides sensitivity.** A dwell is one tone held on one position. With
  the Doppler centre known, the gap at −10 dB-Hz on a 0.1 Hz path is 12.0 dB for a 0.4 s dwell,
  2.2 dB for 4 s and 0.3 dB for 40 s. On a 10 Hz path the three are within 1.5 dB of each other.
- **The long dwells need the path's Doppler shift tracked to a fraction of a hertz.** If a ±1 Hz
  shift is left untracked, the 4 s and 40 s dwells fall to 9.9 and 9.4 dB on the 0.1 Hz path, only
  3 dB better than the 0.4 s dwell, which loses under 1 dB. The 0.4 s dwell is the robust choice
  without a frequency reference; the 4 s dwell is the better one with it.
- **A spread-weighted energy metric is required, and a bank of three or four spread hypotheses is
  enough for the 0.4 s dwell.** One-bin detection loses 6 dB at 10 Hz and 10.5 dB at 30 Hz; matched
  weights lose 1.5 and 1.1 dB.
- **A fresh keyed tone placement in every dwell removes the tone-spacing problem at short dwells.**
  With 256 candidates on adjacent 6.25 Hz positions and a 30 Hz spread, the Monte Carlo shows no
  loss against candidates five positions apart. 256 candidates are 0.2–1 dB better than 64 when a
  symbol carries more than about a third of its bits and equal otherwise.
- **Four bits are enough for the evidence, and the top level is a useful clip.** Sixteen levels of
  half the noise mean lose 0.09 dB of time at weak signals. Four-bit samples lose 0.10 dB when the
  input is noise-dominated.
- **A 24-bit block costs 27% more airtime than a 10-bit block.** The presence test, not the decode,
  sets the size of a short block. With a false-presence rate of 10⁻⁶ per block and 400 timing and
  frequency hypotheses, a block needs a total deflection of about 100 (10 bits) or 127 (24 bits).
- **Estimated block durations** with a 0.4 s dwell: 13–16 s at +10 dB-Hz, 8–12 minutes at 0 dB-Hz,
  11–16 hours at −10 dB-Hz and 45–67 days at −20 dB-Hz on quiet and moderate paths. A tracked 4 s
  dwell brings the last two to 1.2–7 hours and 4–30 days.
- **Without FHSS the same transmitted signal can be received coherently across dwells.** Dwells
  of whole cells back to back are phase-continuous and their phase is known, at no cost to the
  transmitter. On a phase-stable path at −20 dB-Hz the gap falls from 28 dB for per-dwell
  reception to 1.0 dB with 22-minute windows, and a 10-bit block from 44 days to about 2 hours.
  A band of 2 to 16 positions keeps the same symbols and decoder and costs 2.6 dB with 2
  positions, 0.8 dB with 4 and nothing measurable with 16. The price is a block that depends on
  the coherence it was sized for ([details](#without-fhss-contiguous-dwells-coherent-windows-and-few-positions)).
- **With FHSS, the right dwell is the block divided by 32, or 4 over the Doppler spread,
  whichever is shorter.** A block of 32 dwells is within 2.6 dB of a fully coherent block. The
  40 s dwell reaches that only down to −10 dB-Hz on a steady path, so 400 s and 4,000 s are added.
  A new phase per dwell is free; a frequency error per dwell wider than the path's spread costs
  1.3 to 11 times the block time.
- **Long dwells trade concealment for sensitivity against a capable observer.** With 32 dwells per
  block an observer with a matched filter bank needs only 2 to 6 dB more C/N₀ than the link; with
  2,000 dwells, 8 to 14 dB. Against an energy detector the trend is the opposite
  ([details](#with-fhss-how-long-the-dwell-has-to-be-and-what-that-does-to-concealment)).
- **A keyed phase in every cell restores the concealment at almost no receiver cost.** The
  receiver undoes it with an exact quarter turn per cell. The price is timing: hypotheses 10 ms
  apart, where a plain long dwell needs almost none. An observer then needs 12 to 14 dB more C/N₀ than the link at a 40 s
  dwell instead of 2 to 6 dB, and more with longer dwells. Phase changes the receiver does not
  know would cost 3 to 29 times the block time
  ([details](#a-keyed-phase-in-every-cell-concealment-without-giving-up-coherence)).

## 1. The lattice

| Quantity | Value | Reason |
| --- | --- | --- |
| Complex baseband rate | 3,200 Hz (48 kHz ÷ 15) | Holds a 2.8 kHz passband; 512 samples make exactly one FT8 cell |
| Cell | 512 samples, 160 ms | FT8's symbol; short against the coherence time of every mid-latitude class |
| Bin | 6.25 Hz | FT8's spacing; one cell-length FFT gives it without interpolation |
| Tone positions | every bin in the configured band: 352 in 2.2 kHz, up to 448 in 2.8 kHz | The band is occupied anyway, so the alphabet costs no bandwidth |

The FT8/Q65 review explains why this cell has worked on HF
([§2.2](reviews/review-ft8-q65-design.md)): measured 14 MHz spreads are 8–75 mHz on one-hop paths
and 0.13–0.65 Hz on side-scatter, so every mid-latitude class is below a sixth of a bin, and delay
spreads of 0.5–3 ms are under 2% of the cell. It also shows where FT8 stops: with one-bin metrics,
decode probability has a ceiling near 0.66 at 10 Hz of spread, whatever the signal level. Q65 has no
such cliff because it weights the bins around each tone and uses a symbol-metric code. The lattice
keeps FT8's cell and takes Q65's metric.

Whole cells and exact bins are also the conditions under which a simulator may draw bin statistics
directly without forming samples ([review §4.3](reviews/review-dft-fft-computation.md)).

## 2. The dwell

A dwell is one tone held on one position at constant envelope. Three dwell periods were compared.
The flat part is a whole number of cells centred in the period; the edges hold a 20 ms raised-cosine
ramp and a quiet interval for the radio's hop.

| Dwell | Period | Flat part | Guard per edge | Fine resolution |
| --- | ---: | ---: | ---: | ---: |
| A | 0.4 s (1,280 samples) | 2 cells, 0.32 s | 40 ms | 3.125 Hz |
| B | 4 s | 24 cells, 3.84 s | 80 ms | 0.26 Hz |
| C | 40 s | 249 cells, 39.84 s | 80 ms | 0.025 Hz |

Phase and fading are taken as independent from dwell to dwell: the tone position changes by key, and
with FHSS the carrier phase is new in every hop.

### Gap to the bound with the Doppler centre known

Large-alphabet limit, exact, guard time included. `FT8 cell` is a continuous stream of 160 ms cells
with one-bin detection and no guard, for reference. `one bin` is a single bin over the flat part.
`WP` is the spread-matched weighted periodogram.

**C/N₀ = −10 dB-Hz**

| Channel | bound Eb/N₀ | FT8 cell | A one bin | A WP | B WP | C WP |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Stable phase | −1.59 dB | 21.0 | 19.0 | 19.0 | 8.3 | 2.3 |
| Gaussian 0.1 Hz | 5.39 dB | 14.0 | 12.0 | 12.0 | 2.2 | 0.33 |
| Gaussian 0.5 Hz | 11.25 dB | 8.2 | 6.4 | 6.4 | 0.96 | 0.09 |
| Gaussian 1 Hz | 14.09 dB | 5.5 | 4.0 | 4.0 | 0.57 | 0.06 |
| Gaussian 3 Hz | 18.74 dB | 2.1 | 2.9 | 2.4 | 0.31 | 0.03 |
| Gaussian 10 Hz | 23.92 dB | 3.0 | 6.2 | 1.5 | 0.22 | 0.02 |
| Gaussian 30 Hz | 28.68 dB | 6.8 | 10.5 | 1.1 | 0.19 | 0.02 |
| Two modes 1 Hz at ±5 Hz | 17.01 dB | 25.9 | 30.3 | 7.1 | 0.58 | 0.06 |
| Rician K = 1, 1 Hz diffuse | 1.36 dB | 18.2 | 16.4 | 16.4 | 9.2 | 3.3 |
| Auroral 50 Hz at +20 Hz | 30.90 dB | 11.5 | 15.4 | 1.1 | 0.19 | 0.02 |

The gap of dwell A with matched weights at other levels, for the 0.1 / 1 / 10 / 30 Hz channels:
13.3 / 4.1 / 1.5 / 1.1 dB at −20 dB-Hz, 7.8 / 3.4 / 1.5 / 1.1 dB at 0 dB-Hz and
3.1 / 2.0 / 1.4 / 1.1 dB at +10 dB-Hz.

- **About 1 dB of every A entry is guard time** (0.97 dB of energy, which at weak signals is 1.9 dB
  of time; see the envelope table).
- **Dwell A is uniform and expensive on quiet paths.** Its gap is within 1.5 dB from 10 Hz upward and
  grows to 12 dB at 0.1 Hz and 19 dB on a phase-stable path. It never collapses, but it leaves most
  of a quiet path's coherence unused.
- **Dwell B is within 2.2 dB on every Rayleigh channel** from 0.1 to 50 Hz, provided the centre is
  known (next subsection).
- **One-bin detection is a drastic-failure receiver.** On two resolvable modes or an offset auroral
  spectrum it loses 15–30 dB.

### An unknown Doppler shift changes the ranking

Mid-latitude Doppler shift is of order ±1 Hz, with sunrise peaks of +0.6 to +1.7 Hz on 3.5–10 MHz,
and it changes over tens of minutes ([FT8/Q65 review §2.2](reviews/review-ft8-q65-design.md)). The
calculation puts the true centre anywhere within ±1 Hz, at −10 dB-Hz.

| Channel | Dwell | Centre known | One fixed weighting smeared over ±1 Hz | Centre bank, one fine bin apart |
| --- | --- | ---: | ---: | ---: |
| Gaussian 0.1 Hz | A | 12.0 | 12.9 | 13.4 (centred weights) |
| Gaussian 0.1 Hz | B | 2.2 | 9.9 | 3.7 |
| Gaussian 0.1 Hz | C | 0.33 | 9.4 | 0.36 |
| Gaussian 1 Hz | A | 4.0 | 4.8 | 5.2 (centred weights) |
| Gaussian 1 Hz | B | 0.57 | 2.3 | 0.62 |
| Gaussian 1 Hz | C | 0.06 | 1.9 | 0.06 |
| Gaussian 10 Hz | A / B / C | 1.5 / 0.22 / 0.02 | 1.5 / 0.25 / 0.05 | 1.5 / 0.22 / 0.02 |

- Dwell A does not care: its 3.125 Hz bin is wider than the shift.
- Dwells B and C lose 7.7 and 9.0 dB on the quiet path when the shift is absorbed as spread. A bank
  of centre hypotheses recovers it only if the shift stays inside one fine bin (0.26 Hz or 0.025 Hz)
  for as long as the evidence that selects the hypothesis takes to gather. At −10 dB-Hz that
  evidence is a whole block of an hour or more, during which the shift moves.
- **Consequence.** (Superseded on 27 November by
  [the FHSS dwell section](#with-fhss-how-long-the-dwell-has-to-be-and-what-that-does-to-concealment):
  Helper is a diagnostic stand-in for stable oscillators and no dwell may depend on it.) As first
  written: the long dwells need a frequency reference that follows the path: Helper Carrier
  guidance, or a tracking branch that this pass did not design. Without one, the realistic advantage
  of 4 s over 0.4 s on a quiet path is about 3 dB of time, not 10. Equipment drift adds to the same
  requirement: an undisciplined transceiver moves 0.1–1 Hz per minute.
- This sharpens the earlier guidance "configure the dwell from the weakest C/N₀"
  ([fading-limit study, implication 2](../Sol-6.2/fading-limit-and-mfsk-alternatives.md#implications-for-matrix-modem)).
  That guidance assumed a known Doppler centre.

### Spread hypotheses for dwell A

Weights designed for one Gaussian spread (rows) used on another (columns), −10 dB-Hz:

| assumed \ actual | 0.1 Hz | 1 Hz | 3 Hz | 10 Hz | 30 Hz | 60 Hz |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| one bin | 12.0 | 4.0 | 2.9 | 6.2 | 10.5 | 13.4 |
| 3 Hz | 12.6 | 4.5 | 2.4 | 3.4 | 7.0 | 9.8 |
| 10 Hz | 16.7 | 8.3 | 4.4 | 1.5 | 3.1 | 5.6 |
| 30 Hz | 21.3 | 12.7 | 8.2 | 3.5 | 1.1 | 2.0 |
| 60 Hz | 24.3 | 15.6 | 11.1 | 6.0 | 2.1 | 1.1 |

A bank of {one bin, 10 Hz, 30 Hz, 60 Hz}, chosen once per block, stays within 0.6 dB of the matched
receiver from 0.1 to 3 Hz and, by interpolation between the columns, within about 1 dB above that. A single fixed
assumption costs up to 12 dB. Weighting only the tone-position bins, half of the fine bins, costs
nothing up to 1 Hz of spread, 0.3 dB of power at 3 Hz and 1.5 dB from 10 Hz upward; that is the
price of the cheaper transform discussed in the
[computation note](computation-and-simulation-speed.md#what-can-be-traded-for-speed-and-what-it-costs). The choice must be made over the block: choosing per dwell costs
3–11 dB at weak signals ([fading-limit study](../Sol-6.2/fading-limit-and-mfsk-alternatives.md#finite-alphabet-in-a-22-khz-band)).

### Two-stage form of the long dwells

A long dwell can be analysed as cell FFTs followed by a transform across the dwell's cells. Using
only the tone's own cell bin is exact for narrow spreads and loses 3.1 dB at 10 Hz and 6.9 dB at
30 Hz; with both neighbouring cell bins the losses are 1.1 and 2.6 dB; an offset spectrum needs
more. The second stage must therefore take as many cell bins as the widest spread hypothesis
covers. It is then the same weighted periodogram, written as a short matrix on complex bins, which
is the form the [DFT/FFT review](reviews/review-dft-fft-computation.md) recommends.

### Envelope and guard

Static channel, closed form, relative to a tone that fills the whole 0.4 s period:

| Envelope | Receive window | Energy | Evidence (time, weak signals) |
| --- | --- | ---: | ---: |
| 20 ms quiet + 20 ms ramp per edge, 0.32 s flat | rectangular over the 2 cells | −0.97 dB | −1.94 dB |
| same | matched to the envelope | −0.77 dB | −1.54 dB |
| Triad FHSS envelope: 55 ms quiet + 20 ms ramp per edge | rectangular over the 2 cells | −2.44 dB | −4.89 dB |
| same | matched to the envelope | −1.79 dB | −3.58 dB |

- The rectangular window over whole cells is kept because it preserves exact orthogonality and one
  FFT plan; the matched window gains 0.2 dB.
- The existing triad FHSS envelope would cost 1.0–1.5 dB more power-equivalent than the 40 ms guard.
  "Tolerating FHSS ramps" is therefore cheap only if the quiet interval is short.
- For dwells B and C the guard costs 0.18 and 0.02 dB of energy.
- The tone changes position only while the envelope is at zero, so no frequency transition is ever
  transmitted. That avoids the 2.4 dB that FT8's Gaussian shaping (BT = 2) would cost on full-band
  hops ([FT8/Q65 review §6.2](reviews/review-ft8-q65-design.md)), and the 20 ms raised-cosine edges
  put the skirt more than 70 dB down beyond 100 Hz in that review's table.

### Is the fixed cell necessary?

No. Section 9 of the calculation
([output](../../../_appraisal/algorithms-matrixModem/fsk-lattice/results/lattice_receiver_section9.md))
scales the same dwell shape (two cells flat in a period of 2.5 cells) from 40 ms to 11-minute
cells. Gap to the bound in dB of time, spread-matched weights, Doppler centre known:

| C/N₀ | Cell | Phase-stable | 10 mHz | 0.1 Hz | 1 Hz | 10 Hz | 100 Hz |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| −10 dB-Hz | 40 ms | 25.0 | 23.0 | 18.0 | 9.3 | 2.5 | 1.2 |
| −10 dB-Hz | 160 ms | 19.0 | 17.0 | 12.0 | 4.0 | 1.5 | 1.0 |
| −10 dB-Hz | 2.6 s | 8.1 | 6.1 | 2.5 | 1.3 | 1.0 | 1.0 |
| −10 dB-Hz | 41 s | 2.3 | 1.7 | 1.1 | 1.0 | 1.0 | 1.0 |
| −10 dB-Hz | 164 s | 1.5 | 1.2 | 1.0 | 1.0 | 1.0 | 1.0 |
| −20 dB-Hz | 160 ms | 28.9 | 22.0 | 13.3 | 4.1 | 1.5 | 1.0 |
| −20 dB-Hz | 41 s | 6.7 | 2.2 | 1.2 | 1.0 | 1.0 | 1.0 |
| −20 dB-Hz | 164 s | 3.5 | 1.4 | 1.0 | 1.0 | 1.0 | 1.0 |
| −20 dB-Hz | 655 s | 1.9 | 1.1 | 1.0 | 1.0 | 1.0 | 1.0 |
| −30 dB-Hz | 160 ms | 38.9 | 23.2 | 13.4 | 4.1 | 1.5 | 1.0 |
| −30 dB-Hz | 164 s | 9.7 | 1.4 | 1.0 | 1.0 | 1.0 | 1.0 |
| −30 dB-Hz | 655 s | 5.4 | 1.1 | 1.0 | 1.0 | 1.0 | 1.0 |

The floor of 1.0 dB is the guard time.

- **A shorter cell never helps a weak signal.** The 40 ms cell is worse than or equal to the
  160 ms cell in every column, including 100 Hz. Wide spreads are handled by weighting more bins,
  not by shortening the cell.
- **A longer coherent time helps narrow spreads, and the specification already has it for HF** as
  the 4 s and 40 s dwells of whole cells. So for HF from quiet to worst the 160 ms cell is not a
  limitation: at HF Worst (30 Hz) the 0.4 s dwell is 1.1 dB from the bound, and from 100 to 500 Hz
  it is 1.0 dB with matched weights.
- **The spread bank has to reach the spread.** With hypotheses stopping at 60 Hz the loss is
  1.6 dB at 100 Hz, 3.6 dB at 200 Hz and 7.2 dB at 500 Hz; adding 120, 250 and 500 Hz brings all
  three to 1.0–1.1 dB.
- **Strong signals on very wide spreads are capped in rate.** With 100–200 Hz of spread the
  finite-alphabet Monte Carlo gives 0.05–0.12 bits per dwell at +10 dB-Hz (1.1–1.9 dB from the
  bound), 2.6–4.0 bits at +20 dB-Hz (4.0–4.6 dB) and 5.4–6.2 bits at +30 dB-Hz (17 dB). There is
  no cliff; the spread covers 16–32 tone positions and they cannot all be told apart. Tone
  spacing made no difference (256 adjacent against 64 five apart).
- **Phase-stable paths are where per-dwell reception fails.** (First written as "where the fixed
  cell fails", with the scaled lattice as the remedy. Corrected on 27 November: the remedy is
  FHSS off and coherent windows on the default lattice, in the next subsection.) Below 9 kHz the path holds phase for
  hours ([earlier research](../../highFidelity-triple_Robust_Modem/Fable-5.2/10-sub9khz-propagation.md)).
  The default dwell is 19–39 dB from the coherent bound there, the 40 s dwell 2.3–17 dB, and cells
  of 164–655 s are needed to come within 1–5 dB. The antenna also sets the band: practical loaded
  Q gives 6–620 Hz at 8.27 kHz
  ([antenna note](../../highFidelity-triple_Robust_Modem/Fable-5.2/11-sub9khz-antennas-links-and-practice.md)),
  so a 2.2 kHz hop band does not exist. A lattice scaled by 2¹⁰ puts 256 positions in 1.6 Hz.
- **Earth–Moon–Earth sits inside the default lattice** for spread (about 20 Hz per GHz
  limb-to-limb, [EME note](../../highFidelity-triple_Robust_Modem/Fable-5.2/40-eme-lunar-scattering-and-geometry.md))
  and typical levels. What it adds is a Doppler shift of up to ±20 kHz changing at up to 1.3 Hz/s
  at 5.76 GHz, predictable to parts in 10¹⁰, which has to be removed from the samples before the
  transform. Over one 0.4 s dwell the residual motion is half a hertz; over a block it would walk
  across many bins.
- The scaled-lattice figures assume a known Doppler centre. With mHz bins that means disciplined
  references at both ends, as sub-9 kHz practice already uses.

### Without FHSS: contiguous dwells, coherent windows and few positions

27 November 2026. FHSS is optional, and it is not used below 9 kHz, where there is no shift
oscillator. A user there would choose a very narrow band with a few tones and long tones. The
requirement is that the receiver makes the capable attempts while the transmit format stays on the
lattice. Section 10 of the calculation
([output](../../../_appraisal/algorithms-matrixModem/fsk-lattice/results/lattice_receiver_section10.md))
tests this. It replaces the scaled lattice as the answer for phase-stable paths; the scaled-lattice
figures above remain valid as calculations.

**The transmitter changes only by losing the guard.**

- With FHSS every hop retunes the radio. Each dwell then arrives with its own carrier phase, and
  the ramp and quiet interval cover the retune. Without FHSS neither applies.
- Dwells become whole cells back to back: 2, 24 or 250 cells. Every lattice tone completes whole
  cycles in a cell. If every dwell starts at phase zero, the phase is therefore continuous through
  every tone change, the envelope is constant, and the receiver knows the transmitted phase of
  every dwell apart from one carrier phase and the frequency offset. [Derived from the lattice
  rules; it needs the audio band edge on a multiple of 6.25 Hz, as 450 Hz is.]
- Removing the guard gains 0.97 dB at the short dwell. Gap at −10 dB-Hz for 0.1 / 1 / 10 / 30 Hz:
  11.1 / 3.1 / 0.5 / 0.2 dB back to back against 12.0 / 4.0 / 1.5 / 1.1 dB with the guard.
  [Calculated, section 10a.]

**Coherent accumulation across dwells.** The receiver treats `W` consecutive 0.32 s dwells as one
observation with spread-matched weights. Gap to the bound in dB of time, large alphabet, frequency
known to the window's resolution [calculated, section 10b]:

| C/N₀ | Path | 1 dwell | 5 s | 1 min | 22 min | 87 min | 5.8 h |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| −10 dB-Hz | phase-stable | 18.1 | 7.2 | 1.4 | 0.2 | 0.1 | 0.0 |
| −20 dB-Hz | phase-stable | 28.0 | 16.1 | 5.7 | 1.0 | 0.3 | 0.1 |
| −20 dB-Hz | 1 mHz | 26.0 | 14.1 | 3.7 | 0.5 | 0.1 | 0.0 |
| −20 dB-Hz | 10 mHz | 21.0 | 9.1 | 1.3 | 0.1 | 0.0 | 0.0 |
| −20 dB-Hz | 0.1 Hz | 12.3 | 1.9 | 0.2 | 0.0 | 0.0 | 0.0 |
| −20 dB-Hz | 1 Hz | 3.1 | 0.3 | 0.0 | 0.0 | 0.0 | 0.0 |
| −30 dB-Hz | phase-stable | 38.0 | 25.9 | 14.1 | 4.4 | 1.9 | 0.7 |
| −30 dB-Hz | 1 mHz | 31.0 | 19.0 | 7.2 | 1.0 | 0.2 | 0.1 |

- The coherent window is the spread bank continued below the resolution of one dwell. FHSS caps
  that bank at one dwell; without it the cap is the path and the references.
- What matters is the coherent time, not the cell. At −30 dB-Hz a 22-minute window on the default
  lattice is 4.4 dB from the bound. The scaled lattice with a 22-minute flat dwell (`j` = 12) was
  5.4 dB; the difference is its guard.
- On a quiet HF path the short dwell with 5 s windows (1.6 dB at −10 dB-Hz, 0.1 Hz) does what the
  4 s FHSS dwell does (2.2 dB), while the keyed tone still changes every 0.32 s.
- For a path with a spread, the table's long windows use spread-matched weights over the window's
  fine bins. The ladder of the specification approximates that by summing shorter windows as
  energies; the difference was not computed.

**What cross-dwell coherence requires.**

1. *One path phase across the positions in use.* True for a narrow band on one propagation mode.
   Not true over 2.2 kHz of HF multipath: two paths `τ` apart turn the phase through 45° in
   `1/(8τ)` of frequency, which is 250 Hz at 0.5 ms and 60 Hz at 2 ms [derived]. On HF the attempts therefore
   need a band of roughly 8 to 32 positions.
2. *Or one phase per position.* The dwells at each of `Np` positions are summed coherently and the
   `Np` sums combined as energies [calculated, section 10c, phase-stable path]:

   | C/N₀ | Window | All positions together | Np = 2 | Np = 4 | Np = 16 |
   | ---: | --- | ---: | ---: | ---: | ---: |
   | −20 dB-Hz | 22 min | 1.0 | 1.6 | 2.5 | 5.7 |
   | −20 dB-Hz | 5.8 h | 0.1 | 0.2 | 0.3 | 1.0 |
   | −30 dB-Hz | 22 min | 4.4 | 6.4 | 8.7 | 14.1 |
   | −30 dB-Hz | 5.8 h | 0.7 | 1.2 | 1.9 | 4.4 |

   With 352 positions a position recurs once in 352 dwells, so this form helps only with few
   positions. It covers an antenna or path whose phase differs across the band.
3. *Frequency known to a fraction of the reciprocal of the shortest window.* Offsets inside that
   range are resolved by a transform across a message's window sums. Below 9 kHz both ends locked
   to GPS is existing practice, and 1 ppm at 8 kHz is only 8 mHz. An undisciplined reference needs
   a drift search: one 2010 record needed a de-chirp of 43.2 mHz per hour
   ([practice note](../../highFidelity-triple_Robust_Modem/Fable-5.2/11-sub9khz-antennas-links-and-practice.md)).
4. *Timing fine enough for the band.* A timing error `δ` is a phase slope of 2π·span·`δ` across
   the positions: 36° over a 100 Hz span for 1 ms. The timing grid has to be finer than about
   `1/(8·span)` [derived].
5. *A path that holds phase over the window.* The
   [earlier propagation research](../../highFidelity-triple_Robust_Modem/Fable-5.2/10-sub9khz-propagation.md)
   lists coherent integrations of up to 121 hours below 9 kHz, a 168 km path steady within about
   ±10° for hours around midday and wandering by tens of degrees at night, steps of about 40° at
   the end of sunrise on long paths, and flare steps of 20–40°. The decoder documentation quoted
   there says a uniform reference phase usually works, and otherwise splits the message into two
   and then four segments with separate phases.

The window ladder of the specification is the search-free form of that segment split: window sums
are combined as energies, not by a phase search. The cost of combining as energies is small for a
few windows. Total `E/N₀` a 10-bit block needs under the presence test of section 10e: 17.5 dB as
one window, 18.2 dB as four, 19.3 dB as sixteen, 21.0 dB as sixty-four, 25.8 dB as 1,024.

**Few positions.** With fewer than 256 positions the format keeps its 256-value symbols and maps
them onto `P` positions by a keyed balanced map, redrawn every dwell: 256/`P` values share a
position. Tables, code and decoder are unchanged. Two messages share a position in 1/`P` of the
dwells. Monte Carlo of the 10-bit block (32 symbols, 8 dwells per symbol, coherent decode over the
whole block, one unknown phase, 20,000 blocks per row) [simulated, section 10d]:

| Positions | `E/N₀` for 1% block error | Loss | Loss at 0.1% | Asymptotic −10·log₁₀(1 − 1/P) |
| ---: | ---: | ---: | ---: | ---: |
| orthogonal reference | 12.87 dB | – | – | – |
| 256 | 12.84 dB | 0.0 dB | −0.2 dB | 0.02 dB |
| 16 | 12.87 dB | 0.0 dB | −0.1 dB | 0.28 dB |
| 8 | 13.10 dB | 0.2 dB | 0.3 dB | 0.58 dB |
| 4 | 13.66 dB | 0.8 dB | 0.9 dB | 1.25 dB |
| 2 | 15.47 dB | 2.6 dB | 2.6 dB | 3.01 dB |

- The measured loss is below the asymptotic value at these error rates. The 0.1% column rests on
  about 20 errors per point.
- A symbol needs at least 8/log₂`P` dwells so that the block still carries 256 coded bits.
- Two positions are coherent binary FSK, which is 3 dB behind binary phase keying. That is the
  price of a 12.5 Hz band on this lattice. From 8 positions the price is 0.2 dB.
- Per-dwell reception with few positions was not simulated. The coding review's random codebooks
  lost 0.74 dB with 16 tones and 1.42 dB with 8 at 10 bits, in a different regime (strong symbols,
  one look each).

**The antenna sets the number of positions** [derived, section 10f; single-tuned response, no
antenna measured]. Mean radiated power over the positions plus the few-position loss, for the four
antenna cases of the
[earlier antenna note](../../highFidelity-triple_Robust_Modem/Fable-5.2/11-sub9khz-antennas-links-and-practice.md):

| Antenna −3 dB bandwidth | Best choice on the default lattice | Total loss | Phase at the outer position |
| ---: | --- | ---: | ---: |
| 458 Hz | 16 positions (94 Hz) | 0.1 dB | 12° |
| 174 Hz | 8 positions (44 Hz) | 0.4 dB | 14° |
| 57 Hz | 4 positions (19 Hz) | 1.0 dB | 18° |
| 18.5 Hz | 4 positions (19 Hz) | 2.4 dB | 45° |
| 18.5 Hz | `j` = 2: 8 positions of 1.56 Hz (11 Hz) | 0.8 dB | 31° |

So the lattice scale is worth at most 1.6 dB, and only for an antenna narrower than about 50 Hz.
The outer-position phase is static. A transmitter that locks the antenna current's phase removes
it; otherwise the per-position form above or a fixed correction covers it.

(28 November 2026: that judgement weighed antenna loss alone, on four loss cases without a
voltage limit. The [sub-9 kHz rate and spacing note](sub9khz-fsk-rate-and-spacing.md) redoes the
antenna with 10 kW and a voltage limit, adds the mains-harmonic slot, the keyed phase through the
antenna, the per-tone floor and the measured decode work, and recommends scale `j` = 3 with
sixteen positions in 12.5 Hz and 10.24 s tones below 9 kHz. The table above stands as a
calculation.)

**Block duration** [estimate, section 10e]. Presence test at a false-presence rate of 10⁻⁶ over
4,000 hypotheses and a miss rate of 10⁻³, exact chi-square tails, 16 positions, phase-stable path,
one 10-bit block (24 bits take about 25% longer):

| C/N₀ | Per-dwell, 0.32 s | Per-dwell, 40 s | Windows of 1 min | 22 min | 5.8 h | Whole block coherent |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| −10 dB-Hz | 10.7 h | 17 min | 15 min | – | – | 10 min |
| −20 dB-Hz | 44 d | 10.9 h | 6.5 h | 2.2 h | – | 101 min |
| −30 dB-Hz | 12 yr | 36 d | 18 d | 2.0 d | 23 h | 16.8 h |
| −37 dB-Hz | 300 yr | 2 yr | 1.2 yr | 32 d | 6.1 d | 3.5 d |

- The amateur coherent links below 9 kHz listed in the earlier research ran at −15 to −37 dB-Hz.
  Their one-character messages decoded at `Eb/N₀` of +2.6 to +5.4 dB. The 10-bit block's message
  error alone needs 2.9 dB per bit at 1%; the presence test takes it to 7.5 dB. The comparison is
  not like for like: those messages are protected against false decodes by a short CRC (6 bits
  in the record that states it), not by a 10⁻⁶ presence test over 4,000 hypotheses.
- A tenfold increase in hypotheses raises the block time by 0.27 dB at 10 bits and 0.21 dB at 24
  bits [calculated, section 10f]. Further receiver attempts are therefore cheap in sensitivity.
- A block has to be sized for a coherence window. Sizing for 22-minute windows instead of a
  coherent block costs 1.3 times the block time at −20 dB-Hz, 2.9 times at −30 and 9 times at −37.

**Receiver computation.** Per-dwell reception sums one real table per symbol. A coherent attempt
sums complex tables per window, so a ladder whose shortest window is 1/64 of the block is 128 real
columns per timing hypothesis where per-dwell reception has one. Longer windows are sums of
neighbouring window sums per message, and a transform across a message's window sums gives the
frequency offsets, so only the shortest windows are looked up. The work has the shape of the
hypothesis batch already measured (a codebook against table columns), so the measured routes apply:
8,400 full 24-bit decodes a second become about 65 [extrapolated, not run]. That does not limit
live reception, because a block that needs coherent windows lasts hours to days. Bin values are
Gaussian in the noise-dominated case, so the 4-bit sample result (0.10 dB) carries over to 4-bit
bin values [derived, not simulated].

**Disadvantages of leaving FHSS off, and of the attempts.**

1. A second dwell calendar: periods of 0.32, 3.84 and 40 s against 0.4, 4 and 40 s.
2. No quiet interval at a tone change. On a narrow antenna the current takes about `1/(π·BW)` to
   settle: 17 ms at 18.5 Hz, 6 ms at 57 Hz [derived]. Long dwells make that negligible, which is
   one reason to choose them; they also improve the per-dwell fallback (10.9 h against 44 days at
   −20 dB-Hz).
3. No hop diversity and a fixed footprint. With few positions each position is a line that carries
   1/`P` of the power all the time, so the keyed placement hides little from an observer who
   averages a spectrum. Not evaluated.
4. A block sized for coherence fails when the coherence is absent; the per-dwell result is then
   6 to thousands of times too slow. This is the serious one. Size for the shortest window the
   link must survive.
5. Sferics and mains harmonics replace radar as the interference. The
   [earlier noise research](../../highFidelity-triple_Robust_Modem/Fable-5.2/12-sub9khz-noise.md)
   reports about 20 dB from wideband blanking at 8 kHz, clippers preferred over blankers in
   several studies, and harmonics near 8.27 kHz wandering by 2.5–3.1 Hz rms on Continental
   European and US grids and 8–10 Hz on Nordic and British ones. Four positions fit between two
   50 Hz harmonics on the steadier grids. The conditioning chain of this pass was not run on such
   noise.
6. Undisciplined references need a drift search that is not designed.
7. Two and four positions lose 2.6 and 0.8 dB against phase keying.

Not checked: per-dwell reception with few positions; windows under real phase wander; the ladder
against the ideal spread-matched window; 24-bit blocks; the timing slope; any of this in a
receiver.

### With FHSS: how long the dwell has to be, and what that does to concealment

27 November 2026, second question. The intent stated for the dwell options:

- The 0.4 s dwell is for cases such as EME and troposcatter in unlicensed bands, where hopping may
  be a regulatory requirement.
- The longer dwells are meant to reach diminishing returns in both sensitivity, through coherence
  inside the dwell, and LPI. If they are too short for that, they should be longer.
- FHSS is not to be assumed to preserve phase between dwells, and the shift oscillator may also be
  off frequency from one dwell to the next. Helper Carrier is a diagnostic stand-in for more
  stable oscillator hardware, so no dwell may depend on it.

Section 11 of the calculation
([output](../../../_appraisal/algorithms-matrixModem/fsk-lattice/results/lattice_receiver_section11.md))
works this through. Phase is independent from dwell to dwell in all of it.

**1. On a steady path the right dwell is the block divided by 32.** A block has 32 symbols, so at
least 32 dwells. Adding dwells as energies costs more the more dwells share the block's energy.
Total `E/N₀` for a 10-bit block, a false-presence rate of 10⁻⁶ over 4,000 hypotheses and a miss
rate of 10⁻³ [calculated, exact chi-square tails, sections 10e and 11c]:

| Dwells per block | 1 (no FHSS) | 4 | 8 | 16 | 32 | 64 | 128 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `E/N₀` | 17.54 dB | 18.20 dB | 18.70 dB | 19.34 dB | 20.12 dB | 21.04 dB | 22.08 dB |
| Against coherent | – | 0.7 dB | 1.2 dB | 1.8 dB | 2.6 dB | 3.5 dB | 4.5 dB |

- With 32 dwells each dwell holds an energy of 3.2 times the noise density, so the best dwell is
  3.2 divided by the C/N₀ in hertz: 0.32 s at +10 dB-Hz, 32 s at −10, 5.4 minutes at −20 and
  54 minutes at −30.
- A longer dwell than that cannot shorten the block, because of the 32-dwell floor. A shorter one
  pays the combining loss.
- So the long FHSS dwells do capture most of the coherence: all but 2.6 dB of what a fully
  coherent block gets, against 19 to 39 dB for 0.4 s dwells. But 40 s is long enough only down to
  about −10 dB-Hz.

Block time for 10 bits on a steady path, dwell centres known [calculated, section 11c; `*` is the
32-dwell floor]:

| C/N₀ | 0.4 s | 4 s | 40 s | 400 s | 4,000 s | No FHSS, coherent block |
| ---: | --- | --- | --- | --- | --- | --- |
| +10 dB-Hz | 13 s | 2 min * | 21 min * | 3.6 h * | 36 h * | 6 s |
| 0 dB-Hz | 9 min | 2 min * | 21 min * | 3.6 h * | 36 h * | 57 s |
| −10 dB-Hz | 11.8 h | 63 min | 21 min * | 3.6 h * | 36 h * | 9 min |
| −20 dB-Hz | 48 d | 3.4 d | 9.8 h | 3.6 h * | 36 h * | 95 min |
| −30 dB-Hz | 13 yr | 335 d | 32 d | 4.1 d | 36 h * | 15.8 h |

The specification therefore adds 400 s and 4,000 s and makes the dwell adjustable in whole cells.
The named steps are a decade apart: at −20 dB-Hz the 400 s step gives 3.6 hours where a 320 s
dwell would give 2.9.

**2. On a fading path the right dwell is about 4 divided by the Doppler spread, and 40 s already
covers 0.1 Hz.** Block time for 10 bits, section 7 method (Gaussian estimate, 400 hypotheses,
32-dwell floor), dwell centres known [calculated, section 11b]:

| C/N₀ | Spread | 0.4 s | 4 s | 40 s | 400 s | 4,000 s |
| ---: | --- | --- | --- | --- | --- | --- |
| −10 dB-Hz | 10 mHz | 11.1 h | 57 min | 21 min * | 3.6 h * | 36 h * |
| −10 dB-Hz | 0.1 Hz | 11.1 h | 69 min | 45 min | 3.6 h * | 36 h * |
| −10 dB-Hz | 1 Hz | 12.9 h | 5.9 h | 5.2 h | 5.2 h | 36 h * |
| −20 dB-Hz | 10 mHz | 45 d | 3.2 d | 10.9 h | 7.6 h | 36 h * |
| −20 dB-Hz | 0.1 Hz | 45 d | 4.0 d | 2.3 d | 2.2 d | 2.2 d |
| −20 dB-Hz | 1 Hz | 53 d | 24 d | 21 d | 21 d | 21 d |
| −20 dB-Hz | 10 Hz | 287 d | 216 d | 206 d | 205 d | 205 d |

Past about 4 divided by the spread the block time stops falling, because the dwell already
resolves the spectrum. The rule for both kinds of path is the shorter of the two lengths.

**3. A new phase per dwell is free; a new frequency per dwell is not.** Every figure above already
assumes a new phase in each dwell. A frequency error that changes from dwell to dwell has two
regimes [calculated, sections 11b and 11c]:

- *Weak dwells cannot locate themselves.* The best the receiver can do is widen its weights by the
  error range, so the error acts like added Doppler spread. Block time against the centre-known
  case:

  | Path | Dwell | Error within ±0.1 Hz | Within ±1 Hz |
  | --- | --- | ---: | ---: |
  | any | 0.4 s | 1.0× | up to 1.3× |
  | 1 Hz spread | 4 s to 40 s | 1.0× | 1.6× |
  | 0.1 Hz spread | 4 s | 1.3× | 6–7× |
  | 0.1 Hz spread | 40 s | 1.4–1.7× | 8–11× |
  | 10 mHz spread, −20 dB-Hz | 40 s to 400 s | 6–8× | 50× and more |

- *Strong dwells locate themselves.* The statistic is the log of the mean over centre hypotheses
  of the per-centre likelihood, formed inside the dwell. Total `E/N₀` for the 10-bit block on a
  steady path with `K` possible centres per dwell (Monte Carlo, 20,000 blocks per point; the
  method reads 0.3–0.4 dB high at `K` = 1 because its threshold bound is not tight):

  | Dwells per block | `K` = 1 | `K` = 10 | `K` = 100 | `K` = 1,000 |
  | ---: | ---: | ---: | ---: | ---: |
  | 4 | 18.5 dB | 19.3 dB | 19.9 dB | 20.5 dB |
  | 8 | 19.0 dB | 20.3 dB | 21.2 dB | 21.8 dB |
  | 16 | 19.8 dB | 21.5 dB | 22.6 dB | 23.6 dB |
  | 32 | 20.5 dB | 22.7 dB | 24.4 dB | 25.4 dB |
  | 64 | 21.4 dB | 24.4 dB | 26.2 dB | 27.5 dB |

  With 32 dwells, 10, 100 or 1,000 possible centres cost 2.2, 3.9 or 4.9 dB. Fewer, stronger
  dwells pay less: 1,000 centres cost 2.8 dB with 8 dwells and 2.0 dB with 4.
- The two regimes follow from one inequality: the rate with an unknown centre is at least the rate
  with it known minus the log of the number of centres. A dwell must carry much more than that
  log to be worth locating [derived].
- **What this asks of the hardware.** From hop to hop the shift oscillator should come back within
  the path's Doppler spread, or within a few tenths of a hertz. Inside a dwell both oscillators
  must hold to a fraction of the dwell's reciprocal: 25 mHz for 40 s, 2.5 mHz for 400 s, 0.25 mHz
  for 4,000 s. The FT8/Q65 review's figure for an undisciplined transceiver, 0.1–1 Hz per minute,
  is marginal at 4 s and fails from 40 s upward. Long dwells therefore presuppose disciplined
  references, for which Helper is the diagnostic stand-in.
- **Correction.** The consequence drawn under "An unknown Doppler shift changes the ranking"
  above, that the long dwells need Helper guidance or a tracking branch, is replaced by this:
  the hardware supplies the stability, the receiver marginalises each dwell's centre, and a
  path shift common to the block is searched per block.
- **Fewer symbols per block would help long dwells on steady paths.** Sixteen or 8 symbols instead
  of 32 save 0.8 or 1.4 dB with centres known and 1.9 or 3.6 dB with 1,000 centres, and shorten
  the shortest block. Fading paths need the diversity of 32. Left as a decision.

**4. Unlicensed hopping fixes the dwell at 0.4 s.** US rule 47 CFR 15.247 for frequency-hopping
systems, as read on 27 November 2026 from the Cornell LII copy and the GovInfo 2023 annual edition
(the two agree; the current eCFR could not be reached, so later amendments are unchecked):

| Band | Hop frequencies | Average occupancy of a frequency | Peak conducted power |
| --- | --- | --- | --- |
| 902–928 MHz, 20 dB bandwidth under 250 kHz | at least 50 | 0.4 s in any 20 s | 1 W |
| 2,400–2,483.5 MHz | at least 15 | 0.4 s in 0.4 s × the number of channels | 1 W with 75 or more non-overlapping channels, otherwise 0.125 W |
| 5,725–5,850 MHz | at least 75 | 0.4 s in any 30 s | 1 W |

- Hop frequencies are at least 25 kHz or the 20 dB bandwidth apart, used equally on average, from
  a pseudo-random list. There is no minimum bandwidth or data rate for hopping systems.
- Antenna gain above 6 dBi reduces the power dB for dB, except fixed point-to-point links: 1 dB
  per 3 dB at 2.4 GHz, no reduction at 5.8 GHz.
- The 0.4 s dwell period, edges included, equals the occupancy limit. Not examined: whether the
  modem's keyed tone inside a hop channel and the radio's hop schedule satisfy the rule's other
  conditions, and the rules of other countries.
- What the 0.4 s dwell costs there, large alphabet, against the fading bound [calculated,
  section 2]. EME spreads about 20 Hz per GHz, so 18, 49 and 116 Hz in the three bands: 1.0 to
  2.4 dB. Troposcatter fades in proportion to frequency; scaling the 0.01–0.1 Hz at 30 MHz of the
  [earlier groundwave note](../../highFidelity-triple_Robust_Modem/Fable-5.2/25-groundwave-30mhz.md)
  gives roughly 0.3 to 20 Hz [extrapolation]: 6.4 dB at 0.5 Hz, 2.4 dB at 3 Hz and 1.5 dB at 10 Hz
  at −10 dB-Hz, and 2.4, 1.9 and 1.4 dB at +10 dB-Hz.
- On a steady or slow path the 0.4 s dwell is matched to about +10 dB-Hz. Below that the cap
  costs time: 9 minutes against 2 at 0 dB-Hz, 12 hours against 21 minutes at −10 dB-Hz.

**5. Longer dwells help against energy detection and hurt against a matched filter bank.** The
observer of section 11d knows the lattice, the band and the dwell timing but not the key. In each
dwell the tone is in one of `N` cells: tone positions times the hop channels the observer cannot
predict. Its best statistic is the log of the mean likelihood ratio over the cells, summed over the
block.

- For true likelihood ratios the observer's information per dwell is the keyed receiver's minus
  the information the dwell carries about its own position: `D_obs = D_key − I(position; dwell)`
  [derived; exact]. A weak dwell says almost nothing about its position, so the observer gets
  about `1/N` of the keyed receiver's information. A strong dwell says nearly everything, which
  is at most `ln N`, and the observer gets the rest.
- Margin: how much more C/N₀ than the link the observer needs to detect one 10-bit block with
  probability 0.9 at a false-alarm rate of 10⁻² [Monte Carlo with resampled blocks, steady path]:

  | Dwells per block | Block `E/N₀` | Energy per dwell | Filter bank, `N` = 352 | `N` = 17,600 | `N` = 3.5 million | Energy detector, 2.2 kHz for 1 h | 1 MHz for 1 h |
  | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
  | 32 | 20.1 dB | 3.2 | 1.6 dB | 3.8 dB | 5.7 dB | 19.9 dB | 33.2 dB |
  | 128 | 22.1 dB | 1.3 | 3.9 dB | 6.6 dB | 8.8 dB | 18.0 dB | 31.3 dB |
  | 512 | 24.5 dB | 0.54 | 5.8 dB | 9.2 dB | 11.6 dB | 15.6 dB | 28.9 dB |
  | 2,048 | 27.1 dB | 0.25 | 7.5 dB | 11.5 dB | 14.2 dB | 13.0 dB | 26.3 dB |
  | 8,192 | 29.9 dB | 0.12 | 7.9 dB | 13.7 dB | 16.8 dB | 10.1 dB | 23.4 dB |
  | limit of very many weak dwells | – | – | 8.2 dB | 16.7 dB | 28.2 dB | – | – |

- The energy detector's margin is `z·√(W·T)/E` with `z` = 3.61, the same formula as the earlier
  [observer note](../Sol-6.2/observer-and-interference.md). It falls as the dwells shorten because
  the link then needs more power.
- The filter bank's margin rises as the dwells shorten, toward `√N` in amplitude.
- So the stated intent holds against energy detection and fails against a matched filter bank.
  The dwell that maximises sensitivity leaves the key 2 to 6 dB. A block of 500 to 2,000 dwells
  gives up 4 to 7 dB of sensitivity for a margin of 6 to 14 dB. Wide hopping raises the filter-bank
  margin at every dwell length; the limit is reached only with far more dwells than cells.
- Limits of this calculation: a steady path, dwell centres known to both, an observer with perfect
  knowledge of dwell timing and noise level and a filter bank over the whole hop set. Not
  evaluated: fading paths, the periodic dwell envelope as a feature, a noise-uncertain energy
  detector, 24-bit blocks.

### A keyed phase in every cell: concealment without giving up coherence

27 November 2026, third question. The proposal under consideration is that the coherence of the FSK
dwells must not be visible to an adversary, so the phase must change. The concern is what that does
to the receiver's computation, above all for high-sensitivity reception. The brief records why: the
earlier single-carrier Robust modem patterned a tone with keyed phase and amplitude shifts and came
"uncomfortably close" to its computational ceiling. Section 12 of the calculation
([output](../../../_appraisal/algorithms-matrixModem/fsk-lattice/results/lattice_receiver_section12.md))
answers for this waveform.

**The form that fits the lattice.** Each 160 ms cell of a dwell is sent with a keyed phase of 0°,
90°, 180° or 270°. The tone stays on its position for the dwell, the envelope stays constant, and
the phase carries no information.

**Why it costs the receiver almost nothing here.**

- The receiver knows the phase key. It turns each cell's values by the keyed quarter turn before
  it combines the cells of a dwell. A quarter turn is a swap and a sign change, so it is exact on
  4-bit values and needs no multiplier. It can sit in the sample unpacking or between the cell
  transform and the combination over the dwell.
- The turn has unit modulus. With exact timing the tones stay orthogonal over whole cells and the
  noise statistics do not change, so every sensitivity figure of this note holds [derived; exact].
- Transform plan, tone grid, tables, code and decode are untouched. What changes is that the
  cells of a dwell are transformed one by one and then combined (the two-stage form of section 2),
  or the samples are turned before a dwell-long transform.
- The earlier modem's cost had another source. The
  [Opus-5.5 reconciliation](../Opus-5.5/12-reconciliation-and-open-questions.md) attributes it to
  coherent integration and search carried beyond the channel's coherence and drift limits, which
  multiplies hypotheses, and to its computational structure. Here the information is in which
  tone is sent, the phase key is removed before any search, and its step is the FT8 cell, which
  every mid-latitude spread class tolerates.
- For the same reason the failures of direct-sequence modes under 10–30 Hz of spread in the
  [practice review](../Sol-6.2/mfsk-alternatives-practice-and-computation.md) do not carry over:
  those modes need the phase to hold across a long code. Here fading between cells costs exactly
  what it costs a plain tone.

**What it does cost: timing** [calculated, section 12a].

- A receive window displaced by `τ` holds `τ` of the neighbouring cell, whose phase is unrelated.
  The coherent amplitude is 1 − `τ`/160 ms: 0.28 dB at 5 ms, 0.56 dB at 10 ms, 1.16 dB at 20 ms,
  6 dB at 80 ms.
- **Correction (27 November, later the same day).** This list first said that a plain tone loses
  nothing inside the guard and that the phase key needs two to four times the timing hypotheses
  of a 40 ms grid. Both were wrong beyond the 0.4 s dwell. A displaced window on a plain tone
  holds ramp, quiet or the next dwell in place of tone, so the plain tone keeps
  (1 − `τ`/dwell)². The guard keeps the neighbouring tone out; it does not make the error free.
  The general rule is in the next subsection.
- Timing hypotheses 10 ms apart lose 0.14 dB on average (0.28 dB at worst); 20 ms apart lose
  0.27 dB (0.56 dB). Each timing hypothesis repeats the transform and the decode. Ten times the
  hypotheses raises the threshold by about 0.27 dB of block time.
- With timing known from disciplined clocks, which the long dwells need anyway, there is no extra
  hypothesis.
- Timing has to hold to about 10 ms over whatever is combined coherently: 25 ppm over a 400 s
  dwell, 2.5 ppm over 4,000 s, and correspondingly tighter over a coherent window without FHSS.
- A propagation path delayed by 0.5, 2 or 7 ms loses 0.03, 0.11 or 0.39 dB of its own power.

**What sets the timing search, and what drift adds** [calculated, section 12e]. Questions put on
27 November: how much the phase key adds to the timing search at the 6.25 Hz cell, how much worse
unavoidable oscillator and clock drift make it, and what configuration option scales it.

- A timing error `τ` keeps (1 − `τ`/`T`)² of the power. `T` is the stretch over which the
  receiver is coherent with one known phase: the flat part of the dwell for a plain tone, the
  interval between phase steps with the phase key. Hypotheses `T`/8 apart lose 0.27 dB on average
  and 0.56 dB at worst; `T`/16 apart, 0.14 and 0.28 dB. The spacing is a fraction of `T`, not a
  fixed time; "20 ms" is an eighth of the 160 ms cell.
- Spacing at `T`/8, and hypotheses per search dimension against the plain tone:

  | Dwell | Plain tone | Phase step every cell | Every 2 cells | Every 4 cells | Every 8 cells |
  | --- | --- | --- | --- | --- | --- |
  | 0.4 s | 40 ms | 20 ms, 2× | – | – | – |
  | 4 s | 0.48 s | 20 ms, 24× | 40 ms, 12× | 80 ms, 6× | 160 ms, 3× |
  | 40 s | 5 s | 20 ms, 249× | 40 ms, 124× | 80 ms, 62× | 160 ms, 31× |
  | 400 s | 50 s | 20 ms, 2,499× | 40 ms, 1,250× | 80 ms, 625× | 160 ms, 312× |

- The ratios are large because a plain long dwell needs almost no timing search. What the phase
  key costs in absolute terms is the timing uncertainty divided by the spacing, per dimension.
  With timing known to a few milliseconds that is one hypothesis and no added cost.
- A tone that changes position every 160 ms with no guard already has `T` = 160 ms. A phase key
  adds nothing to its search.
- **Oscillator drift adds nothing.** The phase key is removed cell by cell before the frequency
  search, so frequency error and drift are searched exactly as for a plain tone.
- **Clock drift adds a second dimension.** A sample clock off by `e` slips `e` times the block
  time over a block, and one timing hypothesis has to hold over the block. The timing rate
  therefore needs about 2·`e`·(block time)/spacing hypotheses. With offset and rate both uncertain
  the two counts multiply. Rate hypotheses for the 32-dwell block, plain tone / phase step every
  cell / every 4 cells:

  | Dwell | Block | Clock 10⁻⁹ | ±1 ppm | ±20 ppm |
  | --- | --- | --- | --- | --- |
  | 4 s | 2 min | 1 / 1 / 1 | 1 / 1 / 1 | 1 / 1 / 1 |
  | 40 s | 21 min | 1 / 1 / 1 | 1 / 1 / 1 | 1 / 3 / 1 |
  | 400 s | 3.6 h | 1 / 1 / 1 | 1 / 2 / 1 | 1 / 26 / 7 |
  | 4,000 s | 35.6 h | 1 / 1 / 1 | 1 / 13 / 4 | 1 / 256 / 64 |

- A clock whose rate changes during the block (temperature) adds a third dimension, also finer
  with the phase key. Not sized.
- Drift does not turn the keyed phase into the unknown kind of the last paragraph below. It is a
  timing error that grows over the block, and it is searched.
- **The phase interval is the configuration option that scales all of this.** A phase step every
  `k` cells multiplies the spacing by `k` and divides every count above by `k`. The observer can
  then be coherent over `k` cells. Extra C/N₀ the observer needs over the link, largest sum of
  segment energies over a dwell, for 352 / 3.5 million cells:

  | Dwell | Every cell | Every 2 cells | Every 4 cells | Every 8 cells | Plain tone (largest bin) |
  | --- | --- | --- | --- | --- | --- |
  | 4 s | 7.7 / 9.9 dB | 6.5 / 8.8 dB | 5.4 / 7.9 dB | 4.5 / 7.2 dB | 3.3 / 6.3 dB |
  | 40 s | 12.3 / 14.2 dB | 10.9 / 12.8 dB | 9.5 / 11.5 dB | 8.2 / 10.3 dB | 3.3 / 6.3 dB |
  | 400 s | 17.2 / 18.9 dB | 15.7 / 17.5 dB | 14.2 / 16.0 dB | 12.8 / 14.6 dB | 3.3 / 6.3 dB |
  | 4,000 s | 22.1 / 23.8 dB | 20.6 / 22.4 dB | 19.1 / 20.9 dB | 17.6 / 19.4 dB | 3.3 / 6.3 dB |

  Each doubling of the interval returns about 1.4 dB to the observer. The lattice scale would
  also widen the spacing, but it changes the tone bandwidth and the spread tolerance and is not a
  lever for this.
- **The trade at a glance.** At `T`/8 spacing the phase key costs about a third of a dB of
  sensitivity (0.27 dB mean loss and under 0.1 dB of threshold per doubling of hypotheses) and
  whatever the timing search comes to. It buys 8 to 13 dB of concealment at dwells of 40 to 400 s
  with a step every cell.
- Limits: closed form and chi-square tails only. The rate grid is taken to need the same spacing
  as the offset grid. Nothing here was simulated at sample level.

**What an observer loses** [calculated, section 12b]. The link is the 32-dwell block on a steady
path. The observer knows the lattice, band, cell and dwell timing, and wants a detection
probability of 0.9 at a false-alarm rate of 10⁻². Extra C/N₀ it needs over the link, for 352 /
17,600 / 3.5 million cells per dwell:

| Dwell | Plain tone, best statistic (section 11d) | Phase key: largest sum of cell energies over a dwell | Phase key: all energy of the block | Two-phase key, squaring | Four-phase key, fourth power |
| --- | --- | --- | --- | --- | --- |
| 4 s | 1.6 / 3.8 / 5.7 dB | 7.7 / 8.8 / 9.9 dB | 12.6 / 21.1 / 32.6 dB | 10.5 / 11.8 / 13.1 dB | 10.8 / 11.2 / 11.6 dB |
| 40 s | same | 12.3 / 13.2 / 14.2 dB | 17.7 / 26.2 / 37.7 dB | 13.6 / 14.5 / 15.5 dB | 18.4 / 18.8 / 19.2 dB |
| 400 s | same | 17.2 / 18.1 / 18.9 dB | 22.7 / 31.2 / 42.7 dB | 17.9 / 18.7 / 19.6 dB | 25.9 / 26.3 / 26.7 dB |
| 4,000 s | same | 22.1 / 22.9 / 23.8 dB | 27.7 / 36.2 / 47.7 dB | 22.7 / 23.5 / 24.3 dB | 33.4 / 33.8 / 34.2 dB |

- The observer's best option is the largest sum of cell energies: the tone still stays on one
  position for a dwell, and that persistence is what remains visible. Allow about 2 dB for a
  statistic better than the largest-sum detector; for the plain tone the best statistic was 0.6 to
  1.7 dB better than the largest-bin detector.
- With the phase key the margin grows with the dwell, about 5 dB per decade. So the long dwell
  becomes the best choice for sensitivity and for concealment together, which is the brief's
  intent and which the plain tone did not deliver.
- A two-phase key is nearly as good by these numbers, but squaring the signal turns it back into a
  line, which needs no noise reference and gives the frequency away. Four phases cost nothing
  more and push that attack about 5 dB further out at 40 s and 11 dB at 4,000 s. The Gaussian
  sums behind the last two columns are rough at 24 cells.
- The dwell of 0.4 s has two cells; the phase key does nothing useful there.
- The phase steps at the cell rate are themselves a periodic feature. A detector built on it is of
  second order, so in known noise it does not beat the energy figures, but it needs no noise
  reference. Not evaluated.

**Few positions without FHSS** [derived, section 12d]. There every visit to a position continues
the same carrier, so a plain tone leaves a steady line at each of `P` positions with 1/`P`² of the
power: 6, 12, 18 and 24 dB below the signal for 2, 4, 8 and 16 positions. An observer integrating
one such line needs 20·log₁₀`P` dB more C/N₀ than a receiver coherent over the block, and needs no
knowledge of key or timing. The phase key leaves no line. This settles the item left open under
"Disadvantages of leaving FHSS off": without the phase key a few-position signal is barely
concealed at all.

**Phase changes the receiver does not know** [calculated, section 12c]. If the phase changed every
cell and the receiver could not predict it, the receiver would be coherent only inside a cell. The
32-dwell block would need 5.1, 9.7 and 14.6 dB more energy at dwells of 4, 40 and 400 s: 3.2, 9.4
and 29 times the block time. The computation grows with it. The phase change has to be keyed.

**Not checked.** The observer's best statistic with the phase key; fading paths; the cell-rate
feature; phase steps through the transmitter's filter and through a narrow antenna, where each
step rings for about `1/(π·BW)`; whether a keyed phase affects the emission's classification as
spread spectrum (see the [spectrum note](../Sol-6.2/stream-keys-and-spectrum.md)); the timing loss
at sample level.

### Tone length: one cell per tone is the default

27 November 2026, later. The draft had held one tone for the whole hop, so its 0.4 s default sent
2.5 tones per second on a lattice built for 6.25. That came out when the timing spacing of a
"plain tone" was questioned: 40 ms is an eighth of a 320 ms tone, not of the 160 ms cell. The
instruction was that holding a tone longer than its 6.25 Hz bandwidth requires wastes time per
tone, and that the specification be corrected. Tone length and hop dwell are now separate
settings, with one cell per tone as the default. Section 13 of the calculation
([output](../../../_appraisal/algorithms-matrixModem/fsk-lattice/results/lattice_receiver_section13.md))
compares the two.

The receiver is taken as coherent only inside one tone, because different positions do not share
a path phase over a wide band. Extra transmit time against the bound, spread-matched weights,
large alphabet [calculated, section 13a]:

| C/N₀ | Tone | Steady | 0.1 Hz | 1 Hz | 3 Hz | 10 Hz | 30 Hz |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| +10 dB-Hz | 1 cell, 0.4 s hop | 4.9 | 4.5 | 3.1 | 2.2 | 1.8 | 1.3 |
| +10 dB-Hz | 2 cells, 0.4 s hop | 3.6 | 3.1 | 2.0 | 1.9 | 1.4 | 1.1 |
| −10 dB-Hz | 1 cell, 0.4 s hop | 22.0 | 15.0 | 6.5 | 3.1 | 2.0 | 1.3 |
| −10 dB-Hz | 2 cells, 0.4 s hop | 19.0 | 12.0 | 4.0 | 2.4 | 1.5 | 1.1 |
| −10 dB-Hz | 1 cell, no FHSS | 21.0 | 14.0 | 5.5 | 2.1 | 1.0 | 0.3 |
| −10 dB-Hz | 1 cell, 4 s hop | 21.2 | 14.2 | 5.7 | 2.3 | 1.2 | 0.5 |
| −10 dB-Hz | 24 cells, 4 s hop | 8.3 | 2.2 | 0.6 | 0.3 | 0.2 | 0.2 |

Duration of one 10-bit block with the 0.4 s hop [section 13b, section 7 method; `*` is the 32-tone
floor]:

| C/N₀ | Path | 1 cell per tone | 2 cells per tone |
| ---: | --- | --- | --- |
| +20 dB-Hz | any | 6.4 s * | 13 s * |
| +10 dB-Hz | steady to 1 Hz | 16 s | 13 s * |
| 0 dB-Hz | steady to 1 Hz | 14–15 min | 8–9 min |
| −10 dB-Hz | steady to 1 Hz | 22–23 h | 11–13 h |
| −10 dB-Hz | 10 Hz | 3.2 d | 2.9 d |
| −20 dB-Hz | steady to 1 Hz | 91–94 d | 45–53 d |

- **The premise holds for strong signals and not for weak ones.** One cell per tone halves the
  shortest block. From about +10 dB-Hz down it is slower: twice the block time on paths of 1 Hz
  or less, 1.1 times at 10 Hz. In the weak-signal regime evidence grows with the square of the
  coherent time, so two tones of 160 ms gather half of what one tone of 320 ms does.
- **The hop dwell then adds no coherence by itself.** With one-cell tones a 4 s hop is within
  0.8 dB of the 0.4 s hop (14.2 against 15.0 dB at 0.1 Hz); the gain is only the amortised
  edges. The long-dwell results of the sections above therefore apply to tones as long as the
  hop, or to a band narrow enough for the receiver to add different positions coherently.
- **The phase key becomes free in timing.** A timing error on a one-cell tone already moves the
  window onto another tone, so the spacing is 20 ms either way. The multipliers of the previous
  section apply only to tones longer than the phase interval.
- **A phase step at about the tone rate does little for concealment.** The suggestion was that a
  phase change at a rate comparable to the tone rate should give a large LPI benefit before the
  cost climbs. The calculation does not bear that out. The observer's margin grows with the
  number of phase segments in a tone, about 5 dB per tenfold and 1.4 dB per doubling. Two
  segments per tone gain 0.5 to 0.7 dB over a plain tone (4.0 / 5.4 / 6.8 dB against 3.3 / 4.8 /
  6.3 dB for 352 / 17,600 / 3.5 million cells, section 12b) and double the timing hypotheses per
  dimension. The 9 to 13 dB gains belong to held tones of 40 to 400 s with hundreds of segments.
  A one-cell tone on a wide band changes position every cell, so the observer has nothing to add
  coherently and a phase step per tone changes nothing for it. The exception is few positions
  without FHSS: one phase step per tone removes the steady line there, at no timing cost.
  (Corrected later on 27 November: this holds only for observers that measure energy. An
  observer on a phase-stable path can add an unkeyed signal coherently; see "A keyed phase on
  every tone is mandatory" below.)
- **Against a much longer tone** [calculated, section 13]. Extra transmit time against the bound
  at −10 dB-Hz, per-tone reception, steady / 0.1 / 1 / 3 / 10 / 30 Hz: one cell with a 4 s hop
  21.2 / 14.2 / 5.7 / 2.3 / 1.2 / 0.5 dB; a 4 s held tone 8.3 / 2.2 / 0.6 / 0.3 / 0.2 / 0.2 dB.
  Without FHSS: one cell 21.0 / 14.0 / 5.5 / 2.1 / 1.0 / 0.3 dB; a 24-cell tone 8.1 / 2.0 / 0.4 /
  0.1 / 0.0 / 0.0 dB. So the one-cell tone gives up 12 to 13 dB of time on steady and 0.1 Hz
  paths, 5 dB at 1 Hz and 1 dB at 10 Hz, with or without FHSS; FHSS changes the figures only by
  the hop edges. Blocks on a 0.1 Hz path: 18 h against 69 min (4 s tone) and 45 min (40 s) at
  −10 dB-Hz; 75 d against 4.0 d and 2.3 d at −20 dB-Hz. At +10 dB-Hz the order reverses: 13 s
  against 2 min. In the weak-signal regime these times correspond to about half as many dB of
  power. This is the largest single sensitivity cost in the current specification.
- **Where the receiver can add tones coherently, the tone length stops mattering.** The gain then
  follows the coherent window. Section 10b gives 12.3 → 1.9 dB at 0.1 Hz and 3.1 → 0.3 dB at
  1 Hz with 5 s windows at −20 dB-Hz, and 28.0 → 1.0 dB on a steady path with 22-minute
  windows. Those were computed for two-cell tones; the calculation depends on the window, so
  one-cell tones should match [derived, not rerun]. The condition is one path phase across the
  positions in use: tens to a few hundred hertz on HF, or the few-tone sub-9 kHz case. Over
  2.2 kHz of multipath HF neither FHSS setting rescues the short tone.
- **With FHSS the receiver can still add tones coherently, inside one hop.** The radio stays on
  one hop frequency and the tones keep a continuous, known phase. Each hop has a new phase, so
  the window is at most one hop and hops are added as energies. A narrow band with one-cell tones
  and a 400 s hop should then reach about the sensitivity of a 400 s held tone while the tone
  still moves every cell [derived, not computed]. Each hop's frequency error is common to its
  tones and has to be found inside that hop; a weak hop cannot locate itself, and the error then
  acts as added Doppler spread, as in the hop-to-hop figures above.
- **With oscillator drift the receiver can add tones coherently over a window the drift allows.**
  A constant offset is one more search dimension. Drift during the window is the limit: the
  frequency has to stay within a fraction of the window's reciprocal, 25 mHz over 40 s and
  2.5 mHz over 400 s. An undisciplined HF transceiver at 0.1 to 1 Hz per minute is marginal at
  4 s and fails from 40 s, unless a drift-rate search is added, which is not designed. Below
  9 kHz 1 ppm is 8 mHz, and windows of minutes to hours are realistic with GPS-locked references.
  The window ladder needs no decision in advance: a shorter rung wins when drift spoils the
  longer ones, and the per-tone result is the floor. Sample-clock drift is separate and appears
  as timing slip, searched in offset and rate.
- **Diffuse at the transmitter, specular attempted at the receiver** (27 November, as directed).
  The transmitter sends FSK suited to the expected spread of its path class, by default one tone
  per cell on 6.25 Hz spacing, and never relies on coherence between tones. The receiver must
  always attempt to add tones coherently in case the path is specular, and keep the better
  result. This is now a requirement in the specification, not an option.
  - A specular path has one delay, so all positions share a phase apart from a slope across the
    band. The attempt with one phase for all positions is therefore valid over the full 2.2 kHz
    on such a path, which the earlier text ("a band narrower than the coherence bandwidth") did
    not say: that limit is for multipath.
  - The price is timing. The slope needs hypotheses about 1/(8 × span) apart: 57 µs for 2.2 kHz,
    1.25 ms for 100 Hz, 20 ms for two adjacent positions. It is applied to stored bin values
    before summing, so each fine step repeats the accumulation and decode but not the transform.
    Over 2.2 kHz that is about 350 fine steps per 20 ms step, and by the 0.27 dB-per-tenfold rule
    about 0.7 dB of block time in threshold [derived; not sized further, not measured].
  - On a diffuse path the attempts gain nothing and the per-tone result stands.
  - Decided on 27 November: the attempts always run over the whole configured band, not only on
    narrow ones. The work this adds has still to be measured against the computational ceiling.
- **40 s as the alternate hop length for one-cell tones** [from sections 11, 13 and 14]. Supported
  as a compromise: 0.02 dB lost to edges; hopping benefit kept for 50 channels at −10 and
  −20 dB-Hz and for 1,000 at −20 dB-Hz; coherent attempts inside a hop reach a 40 s window,
  which is diminishing returns for 0.1 Hz of spread and more; oscillators need 25 mHz. Its limits:
  1,000 or more channels at −10 dB-Hz want hops of about 5 s or less, and a steady path below
  −10 dB-Hz gains from longer windows (9.8 h at 40 s against 3.6 h at 400 s at −20 dB-Hz).
- **What stays open.** Whether the default should remain one cell for the weak-signal use case,
  or the tone length be set from the design C/N₀. The Monte Carlo sections 4 and 5 and the
  duration estimates of section 5 of this note were run for tones of 2 and 24 cells, not 1.

### One-cell tones with keyed hopping: what an observer needs

27 November 2026, later still. The observation put forward: at a 6.25 Hz tone rate and spacing
there should already be an LPI benefit without any phase change, and FHSS should add to it. Both
hold. The specific case asked for was 400 s hops, one-cell tones, 2.2 kHz, tone placement and hop
frequencies keyed, no phase key. Section 14 of the calculation
([output](../../../_appraisal/algorithms-matrixModem/fsk-lattice/results/lattice_receiver_section14.md))
gives the extra C/N₀ an observer needs over the keyed receiver to notice one 10-bit block.

Model [estimate; Gaussian deflection throughout]. Steady path; receiver coherent inside one tone;
block sized by the exact presence test. The observer knows lattice, band, tone and hop timing, has
a filter bank over every hop channel, and wants 0.9 detection at 10⁻² false alarm. On the occupied
channel its evidence per tone is diluted over 352 positions. Over a hop it adds the tones. Over the
hop channels it has to treat each hop as a mixture.

| C/N₀ | Tones per block | Hop | Hops per block | 1 channel | 50 | 1,000 | 10,000 |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| −10 dB-Hz | 420,000 (18.7 h) | 0.4 s to 40 s | 210,000 to 1,690 | 8.1 dB | > 18 dB | > 18 dB | > 18 dB |
| −10 dB-Hz | | 400 s | 168 | 8.1 dB | 14.8 dB | > 18 dB | > 18 dB |
| −10 dB-Hz | | 4,000 s | 17 | 7.5 dB | 11.5 dB ~ | 12.8 dB ~ | 13.5 dB ~ |
| −20 dB-Hz | 41.6 million (77 d) | 0.4 s to 40 s | 20.8 million to 167,000 | 8.2 dB | 16.7 dB | 23.1–23.2 dB | > 28 dB |
| −20 dB-Hz | | 400 s | 16,700 | 8.2 dB | 16.6 dB | 22.5 dB | 25.5 dB |
| −20 dB-Hz | | 4,000 s | 1,670 | 8.2 dB | 16.3 dB | 20.4 dB | 21.9 dB ~ |
| square-root ideal | | | | 8.2 dB | 16.7 dB | 23.2 dB | 28.2 dB |

`>` means the observer would need tones too strong for the weak-tone formula, so the margin is at
least that. `~` marks figures where a sum is far from Gaussian.

- **The keyed tone placement alone gives about 8 dB.** It is the square root of 352 positions,
  scaled by the observer's looser detection criteria, and it holds at any weak level because both
  sides gather evidence in the same way. This agrees with the Monte Carlo of section 11d, which
  reached 7.9 dB at 8,192 tones per block. No phase change is involved.
  (Corrected later on 27 November: this holds only for observers that measure energy. An
  observer on a phase-stable path can add an unkeyed signal coherently; see "A keyed phase on
  every tone is mandatory" below.)
- **Keyed hopping adds the square root of the channel count, while the observer cannot tell which
  channel is occupied.** That is 8.5 dB for 50 channels and 15 dB for 1,000.
- **A long hop gives part of that back.** A 400 s hop holds 2,500 tones on one channel. At
  −10 dB-Hz that is enough for the observer to find the channel from energy alone, so 50 channels
  give 14.8 dB where the ideal is 16.7 dB; at 4,000 s they give 11.5 dB. Once the channel can be
  found, the protection grows with the logarithm of the channel count. At −20 dB-Hz the tones are
  weak enough that a 400 s hop still gives 16.6 dB.
- **Shorter hops conceal better**, and stay at the ideal wherever the estimate reaches.
- **In time.** Without hopping, an observer at the link's own C/N₀ needs about the square of the
  margin in time, about 40 times the block, because weak-tone evidence grows with the square of
  the power.
- **How short the hop has to be** [derived from the same formula, section 14b]. With
  `x` = z²·channels / (hops per block), the hopping benefit given back is 5·log₁₀(x / ln(1 + x)):
  0.1 dB at `x` = 0.1, 0.8 dB at 1, 1.7 dB at 3, 3.1 dB at 10, 4.7 dB at 30. `x` = 1 is 13 hops
  per block for every hop channel. Longest hop that keeps it there:

  | C/N₀ | Block | 50 channels | 1,000 | 10,000 |
  | ---: | --- | --- | --- | --- |
  | 0 dB-Hz | 13 min | 1.2 s | under 0.4 s | under 0.4 s |
  | −10 dB-Hz | 18.7 h | 103 s | 5.2 s | 0.5 s |
  | −20 dB-Hz | 77 d | 2.8 h | 9 min | 51 s |

  A 40 s hop is therefore short enough for 50 channels at either weak level and for 1,000
  channels at −20 dB-Hz, and not for 1,000 channels at −10 dB-Hz.
- **A shorter hop costs the one-cell receiver only its edges.** The receiver is coherent inside a
  tone, not across the hop. Time lost: 0.97 dB at 0.4 s, 0.18 dB at 4 s, 0.02 dB at 40 s,
  0.002 dB at 400 s.
- **So the trade is not a few decibels of LPI for a drastic loss of sensitivity.** For one-cell
  tones it runs the other way: going from 400 s to 40 s hops costs 0.02 dB and recovers what the
  long hop gave away, about 2 dB with 50 channels at −10 dB-Hz and 0.1 to 0.7 dB at −20 dB-Hz.
  The gain is modest, and so is the price; there is no reason for hops longer than about 40 s.
  Held tones are the opposite case: a shorter hop shortens the tone, and a block at −20 dB-Hz on
  a steady path goes from 3.6 hours at 400 s to 9.8 hours at 40 s.
- **Computational ceiling** [reasoned from the receiver's structure and the stage measurements;
  not measured for these configurations]. With one-cell tones the hop length does not enter the
  receiver's work: one transform per tone per timing hypothesis, timing spacing set by the tone,
  no per-hop frequency search because a new phase is irrelevant to energy detection and an error
  of about ±1 Hz stays inside the 6.25 Hz bin, and the same 32 symbol tables and decode count.
  What moves the ceiling is the tone length. One-cell tones give about 420,000 tones per block
  at −10 dB-Hz against 32 to a few hundred held tones, so more transform and accumulation work,
  in the stage with the most headroom (over 20 million times real time on the GPU); they need
  20 ms timing spacing where a held tone needs an eighth of its length; and they avoid the
  per-hop centre search that held tones of 4 s or more need. The decode per hypothesis is the
  same. Following keyed hops is a matter of retuning or wideband capture, not of computation.
- **Correction of the figures first given in conversation.** A hand estimate put 1,000 and 10,000
  channels at −10 dB-Hz with 400 s hops at 17 and 18 dB. It used the weak-tone formula past its
  range. The calculation gives "at least 18 dB" for both.
- Limits: Gaussian approximations; steady path; hop edges ignored; an observer that knows its
  noise level; a strong-signal block with few hops (0 dB-Hz) is outside the estimate. Fading and
  the periodic hop envelope are not included.

### A keyed phase on every tone is mandatory (27 November 2026)

Decided by the user: at least one keyed phase change per FSK tone, in all cases. The reasoning
below was worked by hand in conversation. It is theoretical and assumes a steady phase at the
observer. The pulse attack and the search volume were computed afterwards as sections 17 and 18
of `lattice_receiver.cpp`; the computed values are at the end of this subsection.

- **Correction of an earlier answer.** I had proposed one-cell tones on a narrow band (32
  positions, 200 Hz) with coherent attempts as a remedy for the slowness of one-cell tones, with
  a block of 45 min to 1 h and an observer margin of about 10 dB at 0.1 Hz and −10 dB-Hz. The
  user pointed out that this fails when the expected spread appears, which is the usual case. A
  narrow band removes only the limit set by multipath delay (about 250 Hz at 0.5 ms of delay
  spread, 60 Hz at 2 ms). Doppler spread still limits coherent adding to about the coherence
  time, so the attempt gains at most what a held tone of about 4 ÷ spread gains. The narrow band
  is the right choice only for paths with very little spread.
- **Whether to keep the one-cell tone given coherent attempts.** Kept as the default. Coherent
  attempts recover the one-cell tone's loss only on a specular path. On a diffuse path of 1 Hz
  or less the matched tone length remains about 4 ÷ spread (4 s at 1 Hz, 40 s at 0.1 Hz), and
  the tone length is best set from the path class's expected spread.
- **What a longer tone costs in concealment** (0.1 Hz, −10 dB-Hz, no hopping, steady-path
  observer; estimates from the stored section 11 to 14 results, not a new run): one-cell tones
  over 2.2 kHz give an 18 h block and 8 dB of observer margin; 40 s held tones 45 min and about
  3 dB; 40 s held tones with a phase step every cell about 10 to 12 dB. At 1 Hz with 4 s tones
  the loss is under 1 dB, because the block still has thousands of tones.
- **What phase steps inside a held tone recover and cost.** A step every cell recovers about
  4 dB (4 s tone) and 9 dB (40 s) at 24 and 249 times the timing hypotheses per dimension of a
  held tone with one phase; a step every 8 cells about 1 and 5 dB at 3 and 31 times. The
  multiplier applies only where timing is uncertain.
- **Per-tone reception of one-cell tones pays nothing for one phase per tone.** Energy per cell
  is unchanged by a quarter turn of the whole cell, so the receiver need not undo it, and the
  20 ms timing spacing is unchanged. An observer measuring energy per cell is equally
  unaffected.
- **The coherent pulse attack.** Every position completes whole cycles in a cell. Without a
  keyed phase every tone starts its cell at the same phase, and the mean of the signal over the
  unknown positions is (1/P)·Σ_k exp(j2πkn/N): a short pulse at each cell start carrying 1/P of
  the tone energy. An observer adds this known pulse train coherently over M cells without any
  position. With per-cell E/N₀ = s and detection index d (about 6):
  - coherent pulse: s ≈ d²·P/M;
  - energy detection over P positions and M cells: s ≈ d·√(P/M);
  - advantage = √(M/P)/d. For P = 352: 1 h (22,500 cells) about 1 dB; 18 h (405,000 cells,
    the −10 dB-Hz block) about 7.5 dB; one week (3.8 million) about 12 dB; 5 dB per tenfold of
    time, without limit, including across blocks.
  This removes the 8 dB of keyed placement within one long block. It needs the path and both
  oscillators steady to a fraction of a cycle over the observation: plausible below 9 kHz, on
  LF ground wave, cable and line of sight. With FHSS it is confined to one hop and gains
  nothing even at 40 s.
- **The user's two scenarios.** HF is not uniform: an observer on a side or back lobe may be
  close enough for ground wave or line of sight and so phase-stable, while the intended
  receiver, perhaps twice as far away with a directional antenna, sees a diffuse path and is
  excluded from coherent reception. And concealment also serves to limit perceived
  interference and complaint: a simple coherent receiver on a short point-to-point link nearby
  could see strong interference from a coherent signal.
- **Remedy.** A keyed quaternary phase per cell makes the mean pulse vanish. Two phases would be
  undone by squaring. The fourth power of a quaternary key is again coherent, but the
  fourth-power loss at these levels far exceeds the gain (not quantified).
- **Cost of more than one phase per one-cell tone** (derived from the timing rule):

  | Phases per tone | Segment | Coarse timing spacing | Transforms where timing is uncertain | Against a filter-bank observer |
  | ---: | ---: | ---: | ---: | ---: |
  | 1 | 160 ms | 20 ms | 1× | baseline |
  | 2 | 80 ms | 10 ms | 2× | about 1.4 dB |
  | 3 | 53 ms | 6.7 ms | 3× | about 2.2 dB |
  | 4 | 40 ms | 5 ms | 4× | about 2.8 dB |

  The key depends only on time, so it is undone on the samples before the transform (signs and
  swaps), or on the transform output with one phase per tone. Per-tone reception pays the same
  multiplier once there is more than one phase per cell. The wide-band coherent attempt's 350
  fine timing steps per coarse step are applied as phase slopes after the transform and do not
  change. Three per cell does not divide 512 samples. Each tone appears 2 to 4 positions wide
  without the key.
- **Tensor Core suitability.** The added work is more timing hypotheses through the same two
  stages. The matrix-product transform is Tensor Core work (70% Tensor activity measured).
  Undoing the key over a held tone's cells is a product with weights of exactly ±1 and ±i, so
  it suits the 4-bit path (derived, not measured). The decode ran at 5,000 full decodes a second
  as a matrix product against 8,400 for the direct kernel, so it is Tensor-compatible but not
  Tensor-optimal as measured.
- **The user's summary of the preference, and my assessment** (27 November; reasoning from
  stored results, nothing computed). The user: a tone rate as fast as the spread and the total
  bandwidth allow, with one keyed phase per tone, is preferred; the exceptions are paths where
  specular reception can be expected, and hardware limited to an extremely narrow band (below
  9 kHz), where either might exceed the computational ceiling.
  - The preference is right for concealment and costs nothing at spreads of about 10 Hz or
    more. At 1 Hz and below it still costs 5 to 13 dB of block time against a tone of about
    4 ÷ spread (open decision 1 of the specification).
  - Nothing is gained by going faster than one tone per cell. Rate cannot exceed spacing, so the
    number of time-frequency cells per second equals the bandwidth at any rate; concealment
    against energy detection and receiver work per second follow that number, not the rate.
  - Specular paths: the load is the fine timing search of the wide-band coherent attempt (57 µs
    at 2.2 kHz, about 350 steps per coarse step), not the tone rate. Slow change of multipath
    delay, as the ionosphere's height changes, is far below the 20 ms coarse spacing but not
    below 57 µs, so it adds a delay-rate dimension to that attempt. Not sized.
  - Below 9 kHz: work per second scales with bandwidth and is tiny. A long phase interval makes
    timing hypotheses coarse (an eighth of the interval), and frequency drift is searched after
    the key is removed. The ceiling is unlikely to bind. The limits there are the phase wander
    that bounds the coherent window, and how fast a phase step can pass a narrow antenna
    (settling of about 1 ÷ bandwidth, with an amplitude dip), which is unexamined.
- **Correction of that assessment, after the user's reply** (27 November; hand estimates, not
  computed). Two of its statements were too narrow.
  - *"The tone rate does not rise."* True only on the fixed lattice, whose fastest rate is
    6.25 Hz. The user means a rate that could approach the total bandwidth where the path
    allows. Rate times positions equals bandwidth, so near 2.2 kHz there is one position and the
    signal is a keyed-phase single carrier (direct sequence), no longer FSK. Timing spacing is
    1/(8 × rate): 20 ms at 6.25 Hz, 1.25 ms at 100 Hz, 57 µs at 2.2 kHz. Clock-rate hypotheses
    for a one-hour block grow with it: 1, 6 and 126 at ±1 ppm; 7, 115 and 2,500 at ±20 ppm. On a
    diffuse path the tone must stay much longer than the delay spread, which holds the rate to
    roughly 50 to 200 Hz on HF. A faster rate improves concealment only for strong signals;
    weak tones are already best observed by energy detection, which depends on bandwidth alone.
  - *"The ceiling is unlikely to bind below 9 kHz."* That counted samples per second, not
    hypotheses. The path's coherence is the main resource there, and using it means frequency
    bins of 1 ÷ (coherent time): 0.28 mHz for one hour. An uncertainty of ±1 Hz is then 7,200
    bins and ±5 Hz 36,000, each a block decode. Drift across the block adds about 4 to 7
    drift-rate bins per millihertz of drift in an hour, growing with the square of the coherent
    time, and the count repeats at every timing step (1.25 s for a 10 s phase interval). At the
    measured 8,400 decodes a second that exceeds real time without narrowing. For scale, at
    9 kHz ±20 ppm is ±0.18 Hz and ±100 ppm is ±0.9 Hz.
  - *Clock corrections.* A disciplined clock is not automatically one hypothesis. A GPS
    correction slewed at 1 ppm moves a 9 kHz carrier by 9 mHz, about 32 one-hour bins; a
    correction stepped by 10 µs is a 32° phase jump at 9 kHz. Corrections applied without the
    receiver's knowledge add drift-rate or phase-step dimensions. Candidate rule, not adopted:
    no clock steps inside a block; corrections slewed smoothly and bounded, or measured, logged
    and removed in the receiver's processing as a known trajectory.
  - Unsized: the full hypothesis volume below 9 kHz (frequency × drift rate × timing offset ×
    timing rate) for the coherence windows of the specification, and what the window ladder
    gives back when the volume is cut by shorter windows.
- **Phase-change rate against FSK rate** (user's expectation of 27 November, with which I
  agree; reasoning, not computed). In a very narrow band a phase change at a given rate costs
  the receiver the same as an FSK change at that rate: both set the stretch of one known phase,
  hence the same timing spacing of an eighth of it, the same drift search, and the same work
  after the transform with the same suitability for GPU hardware. Both also need the same
  bandwidth, so a narrow antenna limits both alike. Where the computational ceiling, the antenna
  or the delay spread limits the rate, phase changes are no alternative to FSK changes.
  - The one case where they differ: a wide band on a diffuse path of low Doppler spread.
    Positions further apart than the coherence bandwidth fade independently, so tones at
    different positions cannot be added coherently, while phase segments inside one held tone
    can. There a faster FSK rate costs sensitivity (5 to 13 dB of time at 1 Hz and below) and
    phase steps inside a held tone do not, at the timing-search cost already recorded.
  - Inside the coherence bandwidth, FSK changes with known phase add as coherently as phase
    segments do, so in the narrow-band case there is no difference in sensitivity either.
- **Computed afterwards** [calculated, sections 17 and 18 of `lattice_receiver.cpp`]. Both hand
  estimates above are superseded where they differ.
  - *Pulse attack.* The hand rule "√(M/P) ÷ 6" was too cautious: the exact test needs
    M·l/P = 8.71 (20.81 with 10,000 frequency bins). Advantage over the section 14 energy
    observer: 0.4 dB in 10 min, 5.2 dB in 1 h, 11.5 dB in 18 h, 16.3 dB in a week, 19.5 dB in
    30 days (with 10,000 bins: −3.4, 1.5, 7.7, 12.5, 15.7). Observer margin over one block:
    +0 dB-Hz 7.5 → 6.1 dB; −10 dB-Hz 8.1 → −3.4 dB; −20 dB-Hz 8.2 → −13.4 dB. The observer
    then detects the block below the level the link works at.
  - *Monte Carlo* (time domain, 1,024 cells, 600 trials): the pulse statistic detects the
    unkeyed signal at 0.86 where 0.90 is predicted (the threshold is an empirical percentile of
    600 trials), and stays at the false-alarm rate with a two-phase or four-phase key. The
    squared pulse detects the unkeyed and the two-phase signal equally (0.47 and 0.49 at
    l = 15 dB, nothing at 10 dB) and nothing with four phases. The fourth power was not run.
  - *Search volume below 9 kHz.* Half-bin frequency spacing loses 0.91 dB at worst and 0.29 dB
    on average. A residual linear drift of 1, 2 and 4 bins across the window loses 0.24, 0.97
    and 4.04 dB, so drift-rate hypotheses go 3.88 bins apart for the same 0.91 dB (3.05 for
    0.56 dB). The hand figure of "4 to 7 drift-rate bins per millihertz in an hour" becomes
    about 2. The statement that the count "repeats at every timing step" assumed a sliding
    search; with a configured calendar the timing factor is the timing uncertainty over an
    eighth of the phase interval, and the result is milder: at 8,400 decodes a second a
    ±20 ppm crystal passes at 1 h and fails at 5.8 h, a ±100 ppm clock fails from 1 h and ±5 Hz
    from 22 min (phase per cell, ±1 s of timing). The reference classes are my assumptions.
- **Keyed phase where a phase change costs the same as a tone change** (user, 27 November):
  it is still worth having, so that only a keyed receiver can add the tones coherently. Agreed;
  it is the reason the requirement holds in the narrow-band case. Hand estimate of the gap on a
  steady path: the coherent keyed receiver needs a total E/N₀ of about 57 for a 10-bit block
  (section 10, "No FHSS, coherent block": 57 s at 0 dB-Hz), and the energy observer needs
  3.61·√(positions × cells). For 4 positions and one hour (22,500 cells) that is 1,083, a gap of
  12.8 dB, rising 5 dB per tenfold of block time. I first said "about 14 dB" in conversation;
  12.8 dB is the corrected figure. Not computed by the program.
- **Specification reconciled again on 27 November** after these additions: section 4.4 regrouped
  (requirement, reasons, receiver cost, purpose of more frequent steps); "phase-continuous"
  removed from section 3; "oscillator drift adds nothing" narrowed to the timing search; the
  diffuse-path line of receiver item 8 aligned with section 4.2; terms for phase key and phase
  interval added; 1 ppm at 9 kHz made 9 mHz throughout.
- **When amplitude shifts would become useful** (user's intuition of 27 November, with which I
  agree in direction; textbook reasoning, nothing computed). Amplitude levels pay when the
  bandwidth, not the power, limits the link: each complex sample offers two dimensions, and
  levels are needed only once a dimension must carry more than about one bit. That is roughly
  above 2 bit/s/Hz, at a symbol energy of about 12 to 15 dB over the noise density. A tone rate
  near the bandwidth is necessary for that but not sufficient; the signal level must be there
  too. In 2.2 kHz it means a C/N₀ of about 46 to 48 dB-Hz, against this specification's +10 to
  −20 dB-Hz, and the default lattice carries 0.023 bit/s/Hz before coding. Below that point a
  peak-limited transmitter loses by varying amplitude, because it lowers the average power.
  Information in phase or amplitude also needs a coherent path. This regime belongs to a fast
  modem, not to this specification.
- **Meteor burst** (user's questions of 27 November; general knowledge and rough link
  arithmetic, not researched, no sources consulted). Not a case for APSK at 2–100 mW: with 160
  to 180 dB of loss on an underdense trail I estimate +10 to +30 dB-Hz at the burst peak, still
  power-limited. The overall FSK design is appropriate: each burst arrives with a new unknown
  phase and frequency offset, as a hop does, and a tone as long as the burst allows collects
  the energy without a carrier. The lattice as specified fits poorly: a burst is about one
  cell, the shortest block is 32 tones, and the evidence clip caps a strong tone. At about
  +20 dB-Hz a 160 ms tone holds roughly 12 dB, about what one 256-value tone needs; stronger or
  shorter bursts want shorter cells (for example 20 ms on 50 Hz spacing), which the
  specification does not offer. The burst's Doppler is mainly a shift of a few hertz to a few
  tens of hertz, and does not itself rule out PSK at 2.2 kHz (about 5° per symbol at 2,000
  baud); the symbol energy of −23 to −3 dB does. Kilowatt systems use PSK. To be researched
  before meteor burst enters section 12.
  (Researched later the same day: the level estimate here was wrong by 15 to 35 dB for small
  antennas. See [the meteor-burst note](meteor-burst.md), which supersedes this bullet.)
- **Delay spread and phase steps** (user, 27 November). Truly diffuse delay spread smears a
  phase step over the spread's length, so steps at a rate comparable to it become noise. Agreed,
  and the same holds for tone changes; it is the 50 to 200 Hz rate limit already recorded. It
  is also the stronger argument against PSK on HF at these levels: 2 ms of delay spread covers
  several symbols at 2,000 baud and needs an equalizer that weak signals cannot train. Loss
  estimated as delay spread ÷ phase interval: 160 ms interval 0.01 / 0.05 / 0.2 dB at 0.5 / 2 /
  7 ms; 80 ms 0.03 / 0.1 / 0.4 dB; 40 ms 0.05 / 0.2 / 0.8 dB. With one phase per one-cell tone
  the step coincides with the tone change and adds no loss. Hand estimates.
- **Open.** The fourth-power statistic and the observer's best statistic against the keyed
  signal; whether 2 or 4 phases per cell are worth offering; measured reference quality below
  9 kHz.

## 3. Alphabet and keyed placement

Monte Carlo of the finite alphabet, 20,000 symbol trials per point. Each dwell places the candidate
tones on a fresh keyed random subset of the grid with a minimum spacing of `k` positions; a symbol
is `L` dwells whose weighted energies are summed; leakage of the transmitted tone into other
candidates is included. Entries are the gap in dB, with the bits carried per symbol in brackets.

**Dwell A (0.4 s), spread-matched weights**

| C/N₀, L | Channel | 64 tones, k = 1 | 64 tones, k = 5 | 256 tones, k = 1 |
| --- | --- | ---: | ---: | ---: |
| +14 dB-Hz, 1 | 0.1 Hz | 6.0 (3.46) | 6.0 (3.46) | 5.1 (4.28) |
| +14 dB-Hz, 1 | 10 Hz | 2.6 (2.85) | 2.6 (2.87) | 2.2 (3.12) |
| +14 dB-Hz, 1 | 30 Hz | 1.8 (1.67) | 1.7 (1.71) | 1.7 (1.73) |
| +6 dB-Hz, 4 | 0.1 Hz | 5.5 (2.14) | 5.5 (2.13) | 5.1 (2.35) |
| +6 dB-Hz, 4 | 30 Hz | 1.4 (0.24) | 1.5 (0.23) | 1.3 (0.24) |
| +2 dB-Hz, 16 | 1 Hz | 3.5 (1.79) | 3.5 (1.81) | 3.3 (1.87) |
| −2 dB-Hz, 64 | 0.1 Hz | 9.1 (1.57) | 9.2 (1.54) | 8.8 (1.66) |
| −2 dB-Hz, 64 | 10 Hz | 1.5 (0.29) | 1.4 (0.29) | 1.3 (0.30) |

One-bin detection in the same runs loses about 6–8 dB at 10 Hz and 10–11.5 dB at 30 Hz.

**Dwell B (4 s), spread-matched weights, centre known**

| C/N₀, L | Channel | 64 tones, k = 1 | 64 tones, k = 5 | 256 tones, k = 1 |
| --- | --- | ---: | ---: | ---: |
| +10 dB-Hz, 1 | 30 Hz | 1.5 (3.40) | 1.3 (3.55) | 1.1 (3.68) |
| +4 dB-Hz, 1 | 0.1 Hz | 5.0 (3.59) | 5.0 (3.60) | 3.9 (4.54) |
| 0 dB-Hz, 1 | 0.1 Hz | 2.6 (2.00) | 2.6 (1.99) | 2.1 (2.24) |
| 0 dB-Hz, 4 | 0.1 Hz | 4.8 (4.81) | 4.8 (4.87) | 3.8 (6.05) |
| 0 dB-Hz, 4 | 1 Hz | 1.6 (3.23) | 1.5 (3.31) | 1.0 (3.64) |
| −6 dB-Hz, 4 | 0.1 Hz | 2.1 (1.28) | 2.3 (1.22) | 1.8 (1.37) |
| −6 dB-Hz, 16 | 1 Hz | 0.7 (1.27) | 0.9 (1.22) | 0.8 (1.23) |

Entries for the 10 and 30 Hz channels at low rates scatter by about ±0.5 dB.

- **Tone spacing does not matter here.** In no row does a spacing of five positions beat adjacent
  positions by more than the Monte Carlo scatter, including 30 Hz of spread. A fresh keyed placement
  per dwell turns neighbour confusion into a small, even rise for every wrong candidate. This agrees
  with the Q65 evidence that an eightfold wider spacing changes the threshold by at most 1 dB once
  the metric is spread-weighted ([FT8/Q65 review §2.1](reviews/review-ft8-q65-design.md)).
- It qualifies the earlier rule "with long dwells, make the spacing several times the widest Doppler
  spread". That rule came from a fixed tone order with one 41 s dwell per symbol, where a 6.25 Hz
  grid lost 6.3 dB at 30 Hz. Dwell C with one or two dwells per symbol is still in that situation and
  was not re-simulated; treat it as a quiet-path option.
- **256 candidates are better than 64 when the symbol is well filled** (0.4–1.1 dB where a symbol
  carries more than about a third of its bits) and equal otherwise. The coding review finds the same
  direction: a keyed 64-ary bank is within 0.2 dB of the orthogonal reference at 10 bits, 16 tones
  are 0.7 dB away and 8 tones 1.4 dB; at 20 bits, 64 tones lose 0.7 dB and 256 tones 0.3 dB ([§1, item 5](reviews/review-coding-interleaving.md)).
- **Repeating a symbol over keyed dwells is as good as a lower code rate** while the symbol carries
  less than about a third of its bits: the gap moves by 0.2–0.5 dB between L = 4 and L = 16 or 64.
  The coding review measures the same thing as an efficiency of at least 0.93
  ([§4](reviews/review-coding-interleaving.md)). The repetition count is therefore the right place
  for the rate reduction that weak signals need, and the code can stay fixed.

## 4. Four-bit evidence and four-bit samples

**Evidence.** Each dwell's weighted energy at a position, divided by a robust noise estimate, is
quantized to `g = min(15, floor(e / step))` and the integers are summed. Closed form for one-bin
evidence:

| Quantizer | Top level reached by noise alone | Loss at weak signals | at 0 dB per dwell | at +5 dB | at +10 dB |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 bit, threshold 1.6 | 20% | 1.89 dB | 2.97 | 5.12 | 8.75 |
| 2 bits, step 1.04 | 4.5% | 0.62 dB | 1.31 | 3.03 | 6.30 |
| 3 bits, step 0.65 | 1.1% | 0.22 dB | 0.65 | 2.03 | 5.00 |
| 4 bits, step 0.40 | 0.27% | 0.08 dB | 0.34 | 1.43 | 4.14 |
| 4 bits, step 0.50 | 0.055% | 0.09 dB | 0.18 | 0.95 | 3.35 |
| 4 bits, step 1.00 | 3×10⁻⁷ | 0.36 dB | 0.15 | 0.20 | 1.43 |

Losses are dB of time; the power equivalent is half.

- At weak signals four bits lose under 0.1 dB and even one bit loses only 1.9 dB. Precision is not
  what limits a long accumulation.
- The top level is a clip, which bounds what any one dwell can contribute. That is the property the
  interference results need ([interference note](othr-and-interference-tolerance.md)).
- A lower clip is more robust to cells that are hit without being flagged, and cheap: a step of
  0.25 (clip at 3.75 times the noise mean) costs 0.19 dB at weak signals. The two reviews put the
  best clip without side information at 2–3 times the noise mean in the weak-look regime and show
  the loss rising with the clip level (2.5–2.9 dB of time at 10% of cells hit for clips of 2–3,
  4.9 dB for a clip of 5).
- The step should follow the design signal level: half the noise mean for dwells below 0 dB, about
  the noise mean times `1 + SNR` above. The configuration is fixed per purpose, so this is a
  constant, not an estimate.

**Samples.** A 15-level code (−7…+7 in steps of 0.45σ) plus a sixteenth code that marks an erased
sample adds −16.3 dB of quantization noise to a noise-dominated input, which is 0.10 dB of
sensitivity (measured on the GPU run, [computation note](computation-and-simulation-speed.md)).
That holds only when nothing strong is left in the band; excision comes first.

## 5. Block size and duration

A block is accepted when its best codeword score exceeds a presence threshold. The coding review
shows that at the point where a 10-bit block decodes with error 10⁻³, the same score tells the block
from noise with only about 10⁻² reliability ([§6](reviews/review-coding-interleaving.md)). Presence,
not decoding, sizes a short block.

With accumulated evidence close to Gaussian, the block needs a total deflection
`S = (z_fa + z_miss)²`, with `z_fa = Q⁻¹(P_fa / (A·2^k))` for `A` timing and frequency hypotheses and
`z_miss = Q⁻¹(P_miss)`. For `P_fa = 10⁻⁶` per block and `P_miss = 10⁻³`:

| Bits `k` | `A` = 1 | `A` = 400 |
| ---: | ---: | ---: |
| 10 | 83 | 100 |
| 16 | 95 | 112 |
| 24 | 110 | 127 |

- From 10 to 24 bits the requirement grows by 27–33%. The exact orthogonal-signalling tables of the
  coding review give the same picture from the decoding side: 10 bits cost 2.0 times one bit at
  10⁻³, and the cost per added bit keeps falling ([§2.4](reviews/review-coding-interleaving.md)).
- 400 hypotheses cost 15–20%. Searching more is cheap in energy and, per the GPU run, in time.
- The number of dwells is `S / d²`, with `d² = 2·GMI` in nat per dwell from the tables of section 2,
  and never fewer than about 32, which is the diversity a short block needs in fading (the coding
  review finds 18–37 branches optimal and a drastic loss with fewer than about 8).

Estimated duration of one block, `A` = 400, Doppler centre known, no interference, no margin:

| C/N₀ | Channel | A: 10 bits | A: 24 bits | B: 10 bits | B: 24 bits | C: 10 bits | C: 24 bits |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| +10 dB-Hz | 0.1 Hz | 13 s | 14 s | 2 min | 2 min | 21 min | 21 min |
| +10 dB-Hz | 10 Hz | 34 s | 44 s | 2 min | 2 min | 21 min | 21 min |
| 0 dB-Hz | 0.1 Hz | 8 min | 10 min | 2 min | 2 min | 21 min | 21 min |
| 0 dB-Hz | 1 Hz | 9 min | 12 min | 5 min | 6 min | 21 min | 21 min |
| 0 dB-Hz | 10 Hz | 43 min | 55 min | 32 min | 41 min | 31 min | 39 min |
| −10 dB-Hz | 0.1 Hz | 11.1 h | 14.1 h | 69 min | 87 min | 45 min | 57 min |
| −10 dB-Hz | 1 Hz | 12.9 h | 16.4 h | 5.9 h | 7.4 h | 5.2 h | 6.6 h |
| −10 dB-Hz | 10 Hz | 2.9 d | 3.7 d | 2.2 d | 2.7 d | 2.1 d | 2.6 d |
| −20 dB-Hz | 0.1 Hz | 45 d | 58 d | 4.0 d | 5.1 d | 2.3 d | 3.0 d |
| −20 dB-Hz | 1 Hz | 53 d | 67 d | 24 d | 30 d | 21 d | 27 d |
| −20 dB-Hz | 10 Hz | 0.8 yr | 1.0 yr | 0.6 yr | 0.7 yr | 0.6 yr | 0.7 yr |

- Entries of 13 s, 2 min and 21 min are the 32-dwell floor: the dwell is longer than the signal
  needs. A dwell that is too long caps the rate; it does not break reception.
- The B and C columns assume the Doppler shift is tracked. Without tracking, the quiet-path B and C
  entries at −10 and −20 dB-Hz move most of the way back to the A column.
- These are about four times the bound's time for the same bits, because a 24-bit block spends
  63 nat on a decision that carries 17 nat of information. By the same estimate a 100-bit block
  would spend about twice its information. The [coding note](coding-and-message-structure.md) covers that trade.
- The durations agree in order with the earlier bound-based estimates: hours to days from −10 to
  −20 dB-Hz on quiet and moderate paths, and no practical service on a 10 Hz path below about
  −10 dB-Hz ([fading-limit study](../Sol-6.2/fading-limit-and-mfsk-alternatives.md#plausible-transmit-time-at-2100-mw-and-150-db-or-more)).

## 6. What the FT8 and Q65 evidence adds

From the [FT8/Q65 review](reviews/review-ft8-q65-design.md):

- **Adopt:** the 6.25 Hz cell; a spread-weighted symbol metric; symbol metrics rather than bit
  metrics (bit metrics lose 0.9 dB at 8 tones and 2.5 dB at 64); likelihoods accumulated over the
  whole message before one decode; percentile noise baselines; a false-accept threshold that rises
  with the logarithm of the number of hypotheses tested.
- **Avoid:** adjacent-tone alphabets with one-bin metrics; FT8's BT = 2 shaping on wide hops;
  a fixed-frequency sync tone; averaging spectra with a capped time constant; check fields sized for
  a few thousand decoder trials.
- **Sync.** FT8 and Q65 spend 26% of their energy on known symbols. In the weak-look regime an
  unknown symbol among `M` candidates contributes only `1/M` of a known tone's sync deflection, so
  sync cannot be read from the data positions alone. The lattice avoids the problem for short blocks
  by decoding every timing and frequency hypothesis exhaustively: the whole codeword is the sync
  pattern, which is Q65's list decoding taken to its limit. That works while the codebook can be
  enumerated. A long-block format will need hopped single-candidate positions (pilots), sized by
  acquisition at the weakest design level.
- **No precedent below about +3 dB-Hz.** Every documented milliwatt HF decode sits at or above the
  WSPR threshold, and the antipodal cases imply 141–156 dB of system loss. The lower 28 dB of the
  target range has no HF precedent; the estimates here rest on theory.

## 7. Reconciliation with earlier findings

| Earlier finding | After this pass |
| --- | --- |
| The 160 ms / 6.25 Hz default is a high-C/N₀ operating point ([Opus-5.5 ch. 5](../Opus-5.5/05-waveform-and-parameter-design.md), [fading-limit study](../Sol-6.2/fading-limit-and-mfsk-alternatives.md)) | Confirmed for a 160 ms *symbol*. The cell is kept as the lattice; the dwell and the repetition count carry the adaptation to weak signals. |
| Configure the dwell from the weakest C/N₀; a longer dwell does not hurt | True with a known Doppler centre. With an untracked ±1 Hz shift the long dwells lose 8–9 dB on quiet paths, so the choice also depends on the frequency reference. |
| With long dwells, tone spacing several times the widest spread | Needed for a fixed tone order with few dwells per symbol. A fresh keyed placement per dwell removes it for dwells A and B. |
| A spread-hypothesis bank spaced by √2–2, chosen per message | Confirmed. For dwell A four hypotheses suffice. |
| Phase-continuous tone transitions | Not needed: the tone changes during the quiet interval. Phase continuity inside the dwell is automatic. |
| Keyed hopping for time diversity, not in-band frequency diversity ([Opus-5.5](../Opus-5.5/05-waveform-and-parameter-design.md)) | Unchanged, with one addition from the OTHR review: hopping inside the passband gives no diversity against an interferer wider than the passband. |
| Four-bit LLRs lose about 0.1 dB | Consistent: 0.09 dB of time for 4-bit energy evidence at weak signals. |

## 8. Weaknesses and open items

- **No coded simulation of the whole chain.** Gaps are information rates; block durations are a
  Gaussian estimate with a floor. A block-error simulation with the actual code, placement, 4-bit
  evidence and presence test is the next calculation. The coding review's bank Monte Carlo is a
  first step.
- **Coherent attempts without FHSS are calculated, not built.** Large-alphabet gaps, a 10-bit
  Monte Carlo with ideal frequency and timing, and a threshold estimate. The ladder under real
  phase wander, the drift search, sferic and mains-harmonic conditioning, and per-dwell reception
  with few positions are open. The scaled lattice is kept only for antennas narrower than about
  50 Hz.
- **The per-block search for the path's Doppler shift is not designed**, and the long dwells
  presuppose oscillators that are steady inside a dwell. Per-dwell frequency errors are covered by
  the calculation of the FHSS dwell section, not by a receiver.
- **Dwell C at wide spreads** was not simulated with a finite alphabet; the earlier 6.3 dB loss at
  30 Hz for one long dwell per symbol on adjacent tones stands as the warning.
- **Fading is independent from dwell to dwell in every calculation except section 10.** On a slow path without FHSS,
  neighbouring dwells fade together. That does not change the mean evidence but it changes the
  spread of block outcomes; the keyed interleaver and block span must supply the diversity.
- **Spread and centre selection per block** is assumed to cost nothing beyond the hypothesis count.
- **Timing.** A 40 ms guard absorbs clock error and ordinary delay spread; long-path echoes of
  40–140 ms fall into the next dwell and were not modelled.
- **Observer detection is calculated for one idealised case only** (steady path, matched filter
  bank or energy detector). The dwell envelope is periodic, which is a further feature an observer
  without the key can integrate on; that and fading paths were not evaluated. The earlier
  [observer notes](../Sol-6.2/observer-and-interference.md) still apply.
- **The presence threshold** assumes Gaussian sums and independent hypotheses. Heavy-tailed evidence
  under interference needs the decoy-calibrated threshold described in the specification.

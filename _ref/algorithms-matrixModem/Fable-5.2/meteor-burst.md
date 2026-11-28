# Meteor burst as a path for the Matrix Modem FSK

Research note of 27 November 2026. It replaces the unsourced meteor-burst remarks made earlier
the same day (recorded in [the lattice design note](fsk-lattice-design.md)). The calculation is
[`meteor_burst.cpp`](../../../_appraisal/algorithms-matrixModem/meteor-burst/meteor_burst.cpp)
with [its results](../../../_appraisal/algorithms-matrixModem/meteor-burst/results/meteor_burst.md).

Tags: **[read]** read in the source during this research; **[calc]** computed here from those
formulas; **[recalled]** remembered, not confirmed in a source. The literature search was done
by a research subagent; I have not re-read the sources myself.

## Conclusions

Stated for 50 MHz, galactic noise and no gain at the transmitter. The sections below are in the
order the work was done; later sections correct earlier ones where marked.

1. **Underdense trails are out of reach at 2–100 mW with small antennas.** The strongest gives a
   peak of −6 dB-Hz at 100 mW and −23 dB-Hz at 2 mW between isotropic antennas over 1,300 km
   [calc]. They need about 17 dBi at each end.
2. **Overdense trails are the working case.** They are under 5% of trails and about 30% of
   throughput on classic links, more as power falls [read]. In 2.2 kHz they present Doppler
   fading of 1–10 Hz and microseconds of delay spread, which is the diffuse case the FSK design
   assumes.
3. **The whole shared sky at 50 to 500 km is the favourable geometry.** 100 mW with no antenna
   gain gets about two block-worth trails an hour; a 20 dBi all-beam receive array raises that
   to about 50 and makes 2 mW workable at 3 to 4 [calc]. At 1,300 km the unaided figure is
   almost nothing.
4. **Up to about 300 km troposcatter is the better path.** It gives 100 mW a continuous −2 to
   +11 dB-Hz at 300 km (median), fading slowly; beyond about 400 km meteor trails win [calc
   from methods not validated at 50 MHz].
5. **A radar at the transmitting site sees about half the link's trails at 50 km and a tenth at
   300 km** [calc]. An illuminator received at the far end sees all of them.
6. **Why a keyed tone and not PSK.** Underdense trails are clean enough for PSK (delay spread
   under 100 ns for 90% of the time) but no burst at these powers holds the energy for a
   carrier; overdense trails fade. A wideband keyed-phase mode on underdense trails is
   physically supported and gains concealment, not sensitivity.
7. **What the specification would need for meteor burst:** a per-trail frequency search of
   about ±30 Hz, a cell shorter than 160 ms, a higher evidence clip, and a block collected over
   one or several trails.
8. **No published result at 10 W or less was found**, and no literature on a higher-frequency
   probe. Operational systems use 300 W to 2 kW.
9. **Corrections of my own estimates made in conversation:** +10 to +30 dB-Hz for 2–100 mW was
   wrong by 15 to 35 dB without antenna gain; the first overdense levels were about 6 dB too
   high; the first short-path rate was too low by a factor of two or more; the troposcatter
   loss was about right.

## Underdense trail: received power and decay

Received power [read, ITU-R P.843-1 eqs. 3–15]:

`p_R(t) = p_T g_T g_R λ² σ a1 a2(t) a2(t0) a3 / (64 π³ R1² R2²)`

- `σ = 4π re² q² L² sin²α`, the echoing area.
- `L² = λ R1 R2 / [(R1 + R2)(1 − sin²φ cos²β)]`, half the principal Fresnel zone, squared.
- `a1 = exp[−8π² r0² / (λ² sec²φ)]`, initial-radius loss, with `log10 r0 = 0.035 h − 3.45`
  (`r0` in m, `h` in km).
- `a2(t) = exp[−32π² D t / (λ² sec²φ)]`, diffusion loss, with `log10 D = 0.067 h − 5.6`
  (`D` in m²/s).
- `a2(t0)`: loss during trail formation, `t0 = (λR / 2V²)^½` for `β` = 90°.
- `a3`: ionospheric absorption, normally under 1 dB.
- Symbols: `p_T` transmit power; `g_T`, `g_R` gains over isotropic; `λ` wavelength; `R1`, `R2`
  distances from the trail to each end; `re` = 2.8×10⁻¹⁵ m; `q` electrons per metre of trail;
  `φ` incidence angle; `β` angle between the trail and the propagation plane; `α` angle between
  the incident field and the direction to the receiver; `V` meteor speed.
- Combined [calc]: `p_R(0) = p_T g_T g_R λ³ re² q² sin²α / [16π² R1 R2 (R1 + R2)(1 − sin²φ
  cos²β)] × a1 a2(t0) a3`. P.843 states the λ³ law.

Decay [read]: the power falls by e² (8.7 dB) in `T_un = λ² sec²φ / (16π² D)`.

Mean trail height [read]: `h = −17 log10 f(MHz) + 124` km; trails lie mainly at 90–110 km.

Computed values, trail above the path midpoint [calc]:

| f (MHz) | Height (km) | Loss at q = 10¹⁴, 500 / 1,000 / 1,300 km (dB) | T_un (s) |
| ---: | ---: | --- | --- |
| 30 | 98.9 | 169.5 / 177.5 / 180.8 | 0.41 / 1.12 / 1.50 |
| 40 | 96.8 | 173.3 / 181.3 / 184.5 | 0.33 / 0.90 / 1.21 |
| 50 | 95.1 | 176.3 / 184.2 / 187.5 | 0.28 / 0.77 / 1.02 |
| 144 | 87.3 | 190.8 / 198.3 / 201.5 | 0.13 / 0.35 / 0.46 |

`T_un` is close to an upper bound. Spann measured time constants of 60 to 109 ms at 50 MHz and
notes that the average duration is well below the classical maximum [read].

Overdense trails: the transition is at about 2×10¹⁴ electrons/m (P.843; other sources use 10¹⁴)
[read]. They last longer and have a rounded envelope; P.843 advises planning on underdense
trails only [read]. Peak power proportional to λ³ q^½ and duration about re q λ² sec²φ / (4π² D)
[recalled, from Sugar 1964].

## How many trails, and how the rate scales

- Flux: `I(q > q0) = 160 / q0` per m² per second, annual average [read, P.843].
- The printed time-of-day factor `[1 + 0.6 sin(πT/12)]` contradicts P.843's own text (maximum at
  06 h, minimum at 18 h local). Check the sign before using it.
- Monthly factor about 0.4 to 1.8; diurnal ratio about 4; seasonal ratio similar, minimum in
  February, maximum in July or August [read].
- Trails against peak amplitude `A`: proportional to `A^−ψ`, with `ψ` = 1.0 at low levels,
  typically 1.1–1.4, above 2 where overdense trails dominate [read].
- About 5% of trails in the best sky region have a usable orientation [read].
- Duty cycle proportional to (threshold power)^−0.6, so to (transmit power)^0.6; average capacity
  proportional to bandwidth^0.4 [read, P.843 eq. 20].
- Frequency: peak power ∝ λ³, decay ∝ λ², duty cycle ∝ λ⁵, galactic noise ∝ λ^2.3 [read].
  Throughput is about twice as high at 40 MHz as at 50 MHz [read, NTIA 89-241].
- Burst rate ∝ (power × gains)^0.5 × f^−1.5 before the noise change [calc].
- Range factor [read, Oetting 1980 as quoted in NTIA 83-116]: 0.58 for 200–480 km; d/770 for
  480–770 km; 1 for 770–1,280 km; 1 − 0.0006 (d − 1,280) for 1,280–2,000 km.
- Arrivals are Poisson and burst durations exponential [read, NTIA 83-116 and 89-241].
- Maximum range 1,800 to 2,250 km depending on the source; the hot spots lie about 100 km to
  either side of the great circle [read].

**Measured anchor** [read, Spann 1987]: 50.050 MHz, 500 W, 17 dBi at each end, 1,300 km, 1.7 kHz
receiver bandwidth, noise floor −125 dBm.

- 91,647 trails in 212 hours, about 432 an hour; 450 an hour used for 1,300 km and 110 an hour
  on a 520 km path with 14 dBi antennas.
- One sample hour: 564 trails, mean duration 413 ms, mean wait 6.5 s, mean peak −109 dBm (range
  about −118 to −91 dBm). 188 of the 564 lasted 50 ms or less.
- Usable time in that hour: 231 s at 6 dB or more above the background, 142 s at 12 dB, 85 s at
  15 dB. That is a 6.4% duty cycle at +6 dB.
- Cross-check [calc]: the P.843 formula predicts −96.5 dBm for q = 10¹⁴ on this link, inside the
  measured range of peaks.

## The channel during a burst

- Doppler: of order 20 Hz at 40 MHz from trail drift; head echoes may span the whole audio band
  [read, P.843 §11]. Scaled linearly: 25–37 Hz at 50 MHz, 70–110 Hz at 144 MHz [calc].
- Delay spread: 12% of underdense and 71% of overdense trails showed any multipath. RMS delay
  spread under 100 ns for 90% of the time and under 400 ns for 99%; rare cases 1–7 µs [read,
  P.843 §12, 500 km link].
- Coherence bandwidth: several megahertz in the main burst, hundreds of kilohertz in wind-shear
  tails [read]. A 2.2 kHz signal sees flat fading.
- Phase: MSK144 adds up to 7 frames (about 0.5 s) coherently, which needs the frequency known
  to better than 1 Hz [read, Franke and Taylor 2017].
- Duration: mean 0.41 s and median below 0.15 s at 50 MHz on the anchor link; 1.0 s mean in a
  30 MHz example. Proportional to f⁻²; under 70 ms is common at 144 MHz [read].
- Faraday rotation is severe below about 40 MHz [read].

## Noise

Galactic noise [read, ITU-R P.372-14 eq. 13]: `F_am = 52 − 23 log10 f(MHz)` dB above kT₀, valid
to about 100 MHz, deciles ±2 dB. That gives −156.0, −158.8 and −161.1 dBm/Hz at 30, 40 and
50 MHz [calc]. Man-made noise, `F = c − d log f`: quiet rural 53.6, 28.6; rural 67.2, 27.7;
residential 72.5, 27.7; city 76.8, 27.7 [read]. At a residential site man-made noise is about
13 dB above galactic at 50 MHz, which worsens every figure below by that much [calc].

## What 2–100 mW gets

Peak C/N₀ from a q = 10¹⁴ trail, 50 MHz, galactic noise [calc]:

| Power | Path | 0 dBi each end | 10 dBi each end | 17 dBi each end |
| --- | --- | ---: | ---: | ---: |
| 2 mW | 500 km | −12.2 dB-Hz | +7.8 | +21.8 |
| 2 mW | 1,300 km | −23.4 | −3.4 | +10.6 |
| 100 mW | 500 km | +4.8 | +24.8 | +38.8 |
| 100 mW | 1,300 km | −6.4 | +13.6 | +27.6 |
| 100 W | 1,300 km | +23.6 | +43.6 | +57.6 |

A weaker trail gives 20 log10(q / 10¹⁴) dB less. The short path has the higher level and far
fewer trails (about a quarter on the measured link).

Bursts an hour that carry one tone's worth of energy (12 dB over the noise density in the whole
burst), 50 MHz, 1,300 km, for the theoretical decay time (1.02 s) and the measured kind (0.1 s)
[calc]:

| Power | Gain each end | Decay 1.02 s | Decay 0.1 s |
| --- | ---: | ---: | ---: |
| 2 mW | 0 or 10 dBi | 0 | 0 |
| 2 mW | 17 dBi | 10 | 0 |
| 100 mW | 0 dBi | 0 | 0 |
| 100 mW | 10 dBi | 15 | 0 |
| 100 mW | 17 dBi | 73 | 23 |
| 10 W | 0 dBi | 15 | 0 |
| 10 W | 10 dBi | 145 | 45 |
| 100 W | 0 dBi | 46 | 14 |
| 100 W | 10 dBi | 459 | 144 |

"0" means the needed trail would be above 2×10¹⁴, outside the underdense formula. Overdense
trails exist and are rarer; they are not counted.

Energy averaged over all underdense trails, as the level of a steady signal delivering the same
energy [calc]: 100 mW gives −30 to −40 dB-Hz at 0 dBi, −10 to −20 dB-Hz at 10 dBi and +4 to
−6 dB-Hz at 17 dBi each end; 2 mW is 17 dB lower. A receiver that adds tones as energies gets
less than this from the many weak bursts, because most of the energy arrives in a few strong
ones.

Limits of these figures: the trail rate of one link with 17 dBi beams is applied to every
antenna (wider beams see more sky and gain less); the scaling runs far outside the measured
range; trail formation and absorption losses are left out; the noise is galactic only.

## Systems and modulation

- Representative operational link: 300 W, 10 dBi, 40–50 MHz [read, NTIA 89-241]. SNOTEL-class
  telemetry: 300–500 W masters, 300 W remotes, low 40 MHz band, 20 kHz channels. COMET used
  35–40 MHz. A FEMA system used 1 kW, BPSK, 4,000 bit/s. Alaska licence limits: 2 kW base,
  500 W remote, PSK or FSK [read].
- Average throughput: up to 100 baud over an hour [read, P.843].
- JANET about 500 W near 40 MHz, COMET about 200 W with 2,000 baud FSK and ARQ [recalled; take
  from Forsyth 1957 and Bartholomé and Vogt 1968 before relying on them].
- Classic systems use PSK and FSK, both coherent and noncoherent [read, NTIA 89-241].
  Noncoherent FSK needs 10 dB S/N for a bit error rate of 10⁻³ [read, NTIA 83-116].
- MSK144: minimum-shift keying at 2,000 baud, 72 ms frames, LDPC (128,80). Nearly all single
  frames decode above 0 dB in 2,500 Hz (+34 dB-Hz); down to about −8 dB with 7 frames added
  coherently [read, Franke and Taylor 2017].
- FSK441: 4-FSK at 441 baud, noncoherent, no error correction, copy a few dB above the noise in
  2,500 Hz [read].
- JT6M: 44-FSK at 21.5 baud [read, secondary]. This is the closest existing mode to the Matrix
  Modem lattice.
- A MITRE comparison found the best noncoherent scheme by average throughput to be 8-ary
  orthogonal FSK, and the best coherent ones 8-ary biorthogonal FSK and QPSK [read, abstract
  only].
- Adaptive rate: 12 times the throughput of a fixed protocol in Spann's thesis, though at four
  times the power [read].
- Low-power observations: the BRAMS beacon (49.97 MHz, 130–150 W) and GB3MBA (50.408 MHz, about
  80 W) are received with small antennas [read]. No numeric result at 10 W or less was found.

## What this means for the specification

Written after the underdense results only. The first bullet is superseded by the overdense and
whole-sky sections below; the current position is in the conclusions above and in section 12.3
of the specification.

- Meteor burst is outside the specification's power range with small antennas, and should not
  be listed as a supported path. With directional antennas at both ends and 100 mW it is a
  marginal erasure channel of tens of tones an hour.
- If it were pursued, the fitting signal is still a noncoherent keyed tone per burst. It would
  need a per-burst frequency search of about ±30 Hz at 50 MHz, a cell near 40–80 ms for the
  median burst, the evidence clip raised, and a block collected over many bursts.
- 40 MHz is better than 50 MHz by about a factor of two in throughput. 144 MHz is 14 dB worse in
  loss, with bursts an eighth as long.

## Proposal: radar-selected trails and a wideband keyed-phase signal

Proposed by the user on 27 November 2026, after the results above. Meteor burst is treated as an
exceptional path: a high-power, highly directional radar near 144 MHz with phased arrays finds
trails that are strongly reflective and long lasting; data follows near 50 MHz at very low
power, with energy collected over many bursts; and because the trail has almost no delay spread,
the signal is a rapid keyed phase sequence over a wide band (hopped, or sampled directly), which
only a keyed receiver can correlate. My assessment follows. It is reasoning from the figures
above; nothing in this section is computed.

- **The physics supports the wideband keyed-phase signal for underdense trails.** Delay spread
  under 100 ns for 90% of the time and a coherence bandwidth of megahertz allow phase steps at
  rates up to about a megahertz. This is the "specular path" exception of the specification
  carried to its end: a tone rate near the bandwidth, which is phase keying.
- **The trails the radar would select are the less clean ones.** 71% of overdense trails showed
  multipath against 12% of underdense, coherence bandwidth falls to hundreds of kilohertz in
  wind-shear tails, and delay spreads of 1–7 µs occur. Long-lasting trails also distort in the
  wind. A rate of about 100 kHz is safe on them; a megahertz is not assured.
- **Concealment is where the gain is, and it is large.** Spreading 2.2 kHz over 1 MHz lowers the
  power density by 27 dB. The transmitter can stay silent between trails. And a trail serves
  only a small ground footprint, so an observer elsewhere needs its own trail.
- **Sensitivity and throughput do not rise at these powers.** The energy a burst delivers is
  the same whatever the bandwidth, and a power-limited link gains no capacity from bandwidth. A
  keyed sequence correlated over a burst and a tone held for the burst collect the same energy.
  The P.843 law of capacity growing with bandwidth^0.4 is for links with signal to spare.
- **Energy per burst remains the limit.** 100 mW with 0 dBi at the transmitter needs about 21
  to 31 dBi at the receiver for a q = 10¹⁴ trail to carry one tone's worth of energy over
  1,300 km at 50 MHz. 30 dBi at 50 MHz is an aperture of about 2,900 m², some 300 elements.
  2 mW needs 17 dB more.
- **A receive array formed digitally keeps the sky coverage that a fixed high-gain antenna
  gives up.** All beams can be formed at once, which is matrix work. That removes the main
  penalty of high gain in the tables above, and is the strongest part of the proposal.
- **Knowing when and where a trail is lets the receiver gate.** Adding energy only while a trail
  is present avoids adding noise during the 94% or more of the time when there is none, and
  bursts can be weighted by their strength as hops are weighted by their noise.
- **A monostatic radar sees different trails.** Forward scatter needs the trail tangent to an
  ellipsoid with the two ends at its foci; a radar looking back along its own line of sight
  sees trails at right angles to that line. The illuminator has to share the link's geometry:
  near one end and received at the other. That is the probe signal of the classic systems. Using
  144 MHz for it is sound: a trail seen there is 14 dB stronger and about 8 times longer-lived
  at 50 MHz.
- **Who has to know.** If the transmitter must know, a signal has to reach it, and the link is
  no longer one-way. If only the receiver knows, the transmitter sends continuously under its
  spreading and the one-way property holds. A strong illuminator beside the low-power
  transmitter would defeat the concealment of that site.
- **Timing is not free.** Clock drift inside a burst is negligible, but each trail is at a
  different place, so the path delay differs from burst to burst by up to about a millisecond.
  At a megahertz that is thousands of delay hypotheses per burst, times some tens of Doppler
  bins for a shift of ±30 Hz. That delay–Doppler correlation is transform and matrix work and
  suits Tensor Core hardware. A trail position from the radar would cut it.
- **Open for this proposal:** the rate and level of overdense trails; measured delay spread and
  phase stability of the long-lasting trails the radar would select; the array size against
  sky coverage; the delay–Doppler search volume per burst.

## Overdense trails as the working case

The figures in this section are superseded by "Overdense trails: researched and computed"
below; the reasoning stands except where that section says otherwise.

The user's reply of 27 November to the assessment above, and my response. The overdense figures
here use formulas I recalled from Sugar 1964 (peak power ∝ λ³ q^½; duration ≈ re q λ² sec²φ /
(4π² D)), not read in a source, and hand arithmetic. They are to be verified.

The user's points:

- Rapid PSK on a clean trail keeps full coherence, unlike FSK on a path with spread.
- Overdense trails are more useful, and must at least be assumed to intrude. Relying on
  underdense trails wastes an opportunity. It also needs more transmit power, which raises the
  noise floor around the transmitter and is the greater risk of being noticed or of causing
  perceived interference.
- A strong 144 MHz illuminator may give away the transmitting site but not the data
  transmission, and an adversary may be unable to get close, for instance across a border.
- Using overdense trails only seems better, and leads back to an FSK modem with rate and
  spacing chosen for diffuse spread; assuming fully diffuse spread is wise. It may also allow
  a practical antenna size.

My response:

- **Coherence.** Agreed for the comparison with the default lattice, which adds one-cell tones
  as energies. Inside one burst a held tone is as coherent as a keyed phase sequence, and
  neither is coherent from one burst to the next, so the wideband form's advantage over a held
  tone is concealment.
- **Overdense trails carry most of the energy.** With trails above q falling as 1/q and energy
  per overdense trail growing as about q^1.5, the energy arriving grows with the square root of
  the largest trail counted. Hand figures for 100 mW, 0 dBi at each end, 50 MHz, 1,300 km, on
  the anchor link's trail rate:

  | q (electrons/m) | Trails an hour above q | Peak C/N₀ | Duration by the formula | Energy over N₀ |
  | ---: | ---: | ---: | ---: | ---: |
  | 2×10¹⁴ (transition) | 8.6 | −0.4 dB-Hz | about 1 s | about −3 dB |
  | 10¹⁵ | 1.7 | +3 dB-Hz | 12 s | about 14 dB |
  | 10¹⁶ | 0.17 | +8 dB-Hz | 116 s | about 28 dB |

  So one trail of 10¹⁵ carries a tone or two, and one of 10¹⁶ could carry a whole 10-bit block,
  about once in six hours, with no antenna gain. Real durations are shortened by wind distortion
  and by loss of electrons, and the trail fades while it lasts; the durations above are upper
  bounds.
- **They push back to FSK, for Doppler spread, not delay spread.** Overdense trails fade as the
  wind distorts them. Their delay spread stays at microseconds and their coherence bandwidth at
  hundreds of kilohertz, so a 2.2 kHz signal still sees flat fading. What a narrowband modem
  meets is a Doppler spread of some hertz and a shift of tens of hertz: the diffuse case the
  specification is built for. Assuming fully diffuse spread is consistent with its principle.
- **The specified lattice fits overdense trails much better than underdense ones.** A trail of
  seconds holds many 160 ms cells, and the shortest block (32 tones, about 5 s) fits inside one
  trail of 10¹⁵ or more. The frequency shift is within the spread bank, which already reaches
  500 Hz with offset for aurora.
- **The wideband keyed-phase mode does not carry over.** It relied on the clean underdense
  trail.
- **Antenna size.** Agreed: the level no longer has to come from gain. Wide antennas also see
  more sky than the anchor link's 17 dBi beams, which raises the trail rate above the table.
- **Illuminator.** The user's judgement on exposure is recorded as stated. Placed at the
  transmitting site and received at the far end, it shares the link's geometry. A trail
  overdense at 50 MHz lasts about an eighth as long at 144 MHz, which is still seconds for
  10¹⁶, so it gives the receiver warning and a measure of strength.
- **Power and local noise.** Agreed in direction: what is noticed near the transmitter scales
  with its power, whatever the trail does. Waiting for strong trails trades time for power.

## Overdense trails: researched and computed

Second literature search and an extension of the calculation (section 5 of the results), done
after the discussion above. Same tags as before. The hand figures in the previous section are
superseded: their levels were about 6 dB too high, because the underdense and overdense
formulas do not join, and their durations were the formula's upper bound.

### Formulas

Received power, Hines–Forsyth form [read in two secondary reproductions that agree: a 2026
simulation paper, eq. 7, and Schilling and Hibshoosh 1988, eq. 2.1.5; Sugar 1964 and Hines and
Forsyth 1957 themselves were not read]:

`P_R(t) = P_T G_T G_R λ² Γ / [32π² R_T R_R (R_T + R_R)(1 − cos²β sin²φ)] × { [(4Dt + r0²) /
sec²φ] · ln[ re q λ² sec²φ / (π² (4Dt + r0²)) ] }^½`

- `Γ` polarization factor; other symbols as for the underdense formula.
- Duration [calc]: `T_ov = re q λ² sec²φ / (4π² D) − r0² / (4D)`. The recalled formula was
  right, with the `r0` term added.
- Peak [calc]: `P_peak = P_T G_T G_R λ³ (re q)^½ Γ / [32π³ e^½ R_T R_R (R_T + R_R)(1 − cos²β
  sin²φ)]`, reached at about `T_ov / e`. λ³ q^½ is confirmed.
- At q = 2×10¹⁴ the overdense peak is 6.4 dB below the underdense one [calc].
- Transition density: about 10¹⁴ in the communications literature, 2×10¹⁴ in P.843, 2.4×10¹⁴
  (Kaiser and Closs 1952, as cited); one source describes mixed behaviour from 10¹³ to 10¹⁶
  [read]. No source gives it a wavelength dependence. The formula adds one through the initial
  radius: a reflecting surface needs q > π² r0² / (re λ² sec²φ), which at 144 MHz is about
  4×10¹³ on a 1,000 km path and 8×10¹⁴ in backscatter [calc].

### What limits real durations

- **Chemistry.** Electrons are lost through reactions with ozone (Baggaley and Cummack 1974, as
  cited). Median overdense duration peaks 1–3 hours after sunrise and is lowest at sunset
  [read].
- **Knee.** A 29.85 MHz radar shows durations turning from diffusion-limited to
  chemistry-limited at about 2.5 s [read]. Diffusion time grows with sec²φ and chemistry does
  not, so a forward-scatter link reaches the chemistry limit at a lower density than a radar
  [calc].
- **Wind.** Long-lived trails are warped by shear, which produces fading and non-specular
  echoes [read].
- **Measured, 45 MHz, 1,150 km, 1 kW (Ostergaard 1990)** [read]: most returns longer than 1 s
  are overdense; the longest ever seen exceeded 40 s, very rarely; between fades few overdense
  trails endure more than one second.
- One simulation caps overdense life at 15 s to represent wind-shear destruction [read].
- Not found: how often echoes exceed 10 s or 60 s, and any 144 MHz duration distribution.

### How many, and how much they carry

- The arrival-rate slope against signal level is about −1 for underdense and −4 for overdense
  trails, predicted and measured [read]. With overdense amplitude ∝ q^¼ that is N(>q) ∝ 1/q
  continuing above the transition [calc], which is what the calculation assumes.
- q against mass: 10⁻³ g ↔ 10¹⁴, 10⁻² g ↔ 10¹⁵, 1 g ↔ 10¹⁷ electrons/m [read, degraded scan].
  2×10¹⁴ corresponds to about 5th magnitude [read].
- **Overdense trails are under 5% of trails and carry about 30% of throughput** (Schanker 1990,
  as cited) [read].
- On the 45 MHz Greenland link the overdense duty cycle exceeded the underdense one at every
  signal level, and "with a transmitter power of 100 W and 10 W the arrival rate will be more
  and more dominated by overdense trails" [read, Ostergaard 1990].
- A 6–30 dB change in sensitivity can move a system from underdense to overdense operation
  [read, NTIA 83-116].
- Not found: overdense counts per hour for beacon networks or amateur bands, and shower against
  sporadic rates.

### Fading, Doppler and multipath

- Overdense trails fade at 1–2.5 fades a second; underdense trails show almost none [read,
  Ostergaard]. Another report gives 2–10 a second within bursts at 50 MHz [read, NBS Technical
  Note 86]. One example began fading after about 0.6 s.
- Doppler shift: of order 20 Hz at 40 MHz [read, P.843]. For a 50 m/s wind the bistatic shift
  is at most about 4 Hz at 50 MHz on a 1,000 km path and 17 Hz in backscatter [calc]. A
  measuring system was built for ±100 Hz [read].
- Multipath: 71% of overdense trails show it. Trail warping gives under 1 µs, occasionally
  5 µs. Two simultaneous trails give up to 1,000 µs on 1–2% of trails; a second component
  within 6 dB was seen on about 6% [read, Weitzen 1986].
- Coherence bandwidth: several hundred kilohertz even with warping [read].
- Not found: a measured Doppler-spread distribution for meteor trails.

### Probe and footprint

- Classic operation: the master repeats a probe, the remote answers on the same or another
  frequency [read, NTIA 89-241].
- From 50 to 144 MHz the peak falls 13.8 dB and the duration 8.3 times [calc from the λ laws].
- No paper was found on a higher-frequency probe predicting a lower-frequency channel, nor on
  monostatic against bistatic detection for communication. That a radar and a link see
  different trails is geometry, not a cited result.
- Footprint of one trail: about 5 × 40 km [read, NTIA 89-241]. Correlation between receivers
  falls to 0.5 at about 150 km along the path and 30 km across it for single underdense trails
  [read, via Schilling and Hibshoosh]. Overdense trails act as larger sources with poorer
  correlation, and a non-specular trail's footprint widens as it expands [read].

### Computed: what 100 mW gets from overdense trails

50 MHz, 100 mW, 0 dBi at each end, galactic noise, trail rate of the anchor link continued as
1/q. Energy over the noise density for the whole echo by the formula, and with the echo cut at
15 s and at 3 s [calc]:

| Path | q (e/m) | Trails an hour above q | Peak C/N₀ | T_ov | Whole echo | Cut at 15 s | Cut at 3 s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,300 km | 10¹⁵ | 1.7 | −3.1 dB-Hz | 12 s | 6.5 dB | 6.5 dB | 0.7 dB |
| 1,300 km | 3×10¹⁵ | 0.56 | −0.7 | 35 s | 13.7 | 10.4 | 1.6 |
| 1,300 km | 10¹⁶ | 0.17 | +1.9 | 115 s | 21.5 | 11.8 | 2.4 |
| 500 km | 10¹⁵ | 0.41 | +8.6 | 3.1 s | 12.6 | 12.6 | 12.5 |
| 500 km | 3×10¹⁵ | 0.14 | +11.0 | 9.4 s | 19.7 | 19.7 | 15.0 |
| 500 km | 10¹⁶ | 0.04 | +13.6 | 31 s | 27.6 | 24.8 | 16.1 |

One 256-value tone needs about 12 dB and a 10-bit block received per tone on a steady path
about 20 dB; a fading echo needs more. Other powers and gains add their decibels: 10 dBi at each
end adds 20 dB, and 2 mW takes off 17 dB.

Reading it:

- **With no antenna gain, 100 mW gets about one tone's worth from a strong overdense trail:**
  roughly one every two to six hours, on either path length. A whole block in one trail needs
  10¹⁶ or more with an echo lasting tens of seconds, which is rare and may not occur.
- **With 10 dBi at each end, every overdense trail carries a block's worth:** 22 dB already at
  5×10¹⁴ on the long path, about 3 an hour.
- **At 2 mW, 10 dBi at each end gives about what 100 mW gives with none.**
- **The short path has the level, the long path the trails and the durations.**
- **The realistic duration matters more than anything else in the table.** Between 3 s and the
  formula's value the energy of a strong trail differs by 10 to 20 dB.

### What changes in the earlier response

- "One trail of 10¹⁶ could carry a whole 10-bit block about once in six hours with no antenna
  gain" was too favourable. By the formula it is marginal (21.5 dB on the long path) and with
  a realistic duration it is one tone.
- The statement that overdense trails carry most of the energy is supported: under 5% of trails,
  about 30% of throughput on classic links, and a growing share as power falls.
- The statement that the narrowband signal meets Doppler spread, not delay spread, is
  supported: fading at 1–10 a second, delay spread of microseconds.
- The fading rate of 1–10 Hz and the shift of up to tens of hertz put the one-cell tone
  (160 ms, 6.25 Hz) at the edge: several fades can fall inside one tone at the fast end. The
  spread bank covers it; a shorter cell would fit better.

## Short paths with sky-scanning arrays

The hand figures in this section are superseded by "Whole shared sky: computed" below. They
were too low for the unaided link (I estimated under one block-worth trail an hour; the
calculation gives about two).

Proposed by the user on 27 November: phased arrays that scan the sky, with a high-power 144 MHz
radar, might use every adequate overdense trail in the whole shared sky, over a distance of
hundreds of miles or less instead of more than a thousand. My assessment is hand arithmetic
from the formulas and figures above; it is not computed by the program, whose geometry places
the trail above the path midpoint.

- **The shared sky is nearly the whole sky.** A trail at 95 km is above the horizon out to about
  1,100 km, so two stations a few hundred kilometres apart see almost the same trails. An array
  that forms all beams removes the limit a fixed antenna's beam sets on the trail rate.
- **Rate, from the P.843 flux** (160/q per m² per second, and about 5% of trails usefully
  oriented): within 300 km of the midpoint, about 8 usable trails an hour above 10¹⁵ and about
  40 above 2×10¹⁴. The measured long link with 17 dBi beams gave 1.7 and 8.5.
- **Level rises, duration falls.** With trail distances near 200 km in place of 660 km the
  level is about 16 dB higher. The forward-scatter angle is small, so the formula's durations
  shrink to roughly 0.3–0.6 s at 10¹⁵ and 3–6 s at 10¹⁶. Those are short enough to be
  realistic, so the largest uncertainty of the long-path figures mostly goes away.
- **What 100 mW then gets, no gain at the transmitter:** a 10¹⁵ trail gives about +12 dB-Hz for
  half a second, about 8 dB of energy; a 10¹⁶ trail about +17 dB-Hz for some seconds, about
  23 dB, a block's worth, under one an hour. With a 20 dBi receive array every trail above 10¹⁵
  carries a block's worth (about 8 an hour) and trails above 2×10¹⁴ carry a tone or more (about
  40 an hour).
- **The radar's geometry objection weakens at short range.** As the baseline shrinks against
  the trail distance, the forward-scatter condition approaches the backscatter one, so a radar
  at the transmitting site sees more nearly the trails the link uses. For a 150 km baseline and
  a trail 300 km away the required trail orientation still differs by up to about 14°, which
  moves the reflecting point by more than a typical trail's length; for 50 km it is within
  it. Long-lived overdense trails also become non-specular as the wind warps them and then
  reflect over a wide range of directions.
- **At 144 MHz a trail needs about 8×10¹⁴ electrons/m to behave as overdense in backscatter.**
  Weaker trails give only a brief underdense echo there, which a high-power radar still sees.
- **A competing path appears at these distances.** Troposcatter is continuous out to a few
  hundred kilometres. I recall its loss at 50 MHz and 300 km as roughly 170 to 180 dB, which
  would give 100 mW about 0 to +6 dB-Hz continuously with no antenna gain, more than meteor
  trails deliver on average. That figure is recalled, not checked, and troposcatter has a wide
  footprint where a trail's is small.
- The troposcatter figure was checked afterwards; see "Troposcatter as the competing path".

## Whole shared sky: computed

[`meteor_sky.cpp`](../../../_appraisal/algorithms-matrixModem/meteor-burst/meteor_sky.cpp) with
[its results](../../../_appraisal/algorithms-matrixModem/meteor-burst/results/meteor_sky.md)
[calc]. A Monte Carlo over trail position and direction on a spherical Earth: straight 25 km
trails at 95 km, the P.843 flux of 160/q per m² per second continued to 10¹⁶ electrons/m,
directions as an isotropic sky delivers them through a horizontal surface, the forward-scatter
condition tested on each trail, both stations required to see the reflecting point at 3° or
more, the underdense formula below 7.5×10¹³ and the overdense one above with echoes cut at 15 s.
50 MHz, galactic noise, 0 dBi at the transmitter, the receive gain available in every direction.
Fading inside an echo, absorption, polarization loss and the annual and daily variation of the
flux are left out.

**Check.** For the measured long link (500 W, 17 dBi at each end, 1,300 km, 450 trails an hour
above −125 dBm) the model gives 1,063 an hour with 17 dBi applied over the whole sky. The real
beams covered part of the sky, so a higher model figure is expected; the factor of 2.4 is
plausible but not a validation.

Usable trails an hour, by what one echo carries (a tone's worth 12 dB, a block's worth 20 dB, a
block with 6 dB allowed for fading 26 dB):

| Distance | Power, receive gain | Tone | Block | Block with fading allowance | Mean echo at the block threshold |
| ---: | --- | ---: | ---: | ---: | ---: |
| 50 km | 100 mW, 0 dBi | 9.1 | 2.0 | 0.5 | 3.2 s |
| 50 km | 100 mW, 10 dBi | 43 | 12.8 | 4.4 | 1.5 s |
| 50 km | 100 mW, 20 dBi | 148 | 57 | 24 | 0.4 s |
| 50 km | 2 mW, 20 dBi | 15 | 3.7 | 1.1 | 2.6 s |
| 150 km | 100 mW, 0 dBi | 8.9 | 2.0 | 0.5 | 3.6 s |
| 150 km | 100 mW, 20 dBi | 148 | 56 | 24 | 0.4 s |
| 300 km | 100 mW, 0 dBi | 8.2 | 1.7 | 0.4 | 5.0 s |
| 300 km | 100 mW, 10 dBi | 40 | 11.5 | 3.9 | 1.8 s |
| 300 km | 100 mW, 20 dBi | 146 | 53 | 22 | 0.4 s |
| 300 km | 2 mW, 20 dBi | 13.6 | 3.2 | 0.9 | 3.7 s |
| 500 km | 100 mW, 0 dBi | 7.6 | 1.6 | 0.1 | 8.5 s |
| 500 km | 100 mW, 20 dBi | 142 | 50 | 20 | 0.5 s |
| 1,000 km | 100 mW, 0 dBi | 3.5 | 0.1 | 0 | 13 s |
| 1,000 km | 100 mW, 20 dBi | 72 | 23 | 9.2 | 1.7 s |
| 1,300 km | 100 mW, 0 dBi | 0.6 | 0.01 | 0 | 14 s |
| 1,300 km | 100 mW, 20 dBi | 24 | 7.2 | 2.9 | 3.1 s |

2 mW with no receive gain gets at most 0.2 tone-worth trails an hour at any distance, and no
block.

Reading it:

- **The user's expectation holds: the whole shared sky at a few hundred kilometres is far
  better than a long path.** From 50 to 500 km the figures hardly change with distance; beyond
  that they fall quickly.
- **100 mW with no antenna gain at either end gets about two block-worth trails an hour** and
  eight or nine tone-worth trails, at 50 to 500 km. With the fading allowance it is one block
  every two to ten hours.
- **A 20 dBi all-beam receive array raises that to about 50 blocks an hour**, or 20 with the
  fading allowance, and makes 2 mW workable at 3 to 4 an hour.
- **Echoes are short where the link has gain** (0.4 s at 20 dBi) because weaker, more numerous
  trails then suffice, and seconds long where it has none. The 15 s cut rarely matters below
  500 km.
- **Only about 1% of trails in view meet the scatter condition** (1.3% at 50 km, 0.7% at
  500 km, 0.1% at 1,300 km). The "5% in the best region" of P.843 is not the whole-sky figure.

**A radar at the transmitting site.** Share of the link's block-worth trails that also give a
specular echo to a radar at the transmitter:

| Distance between stations | 50 km | 150 km | 300 km | 500 km | 1,000 km |
| --- | ---: | ---: | ---: | ---: | ---: |
| Also seen by the radar | 53–57% | 17–21% | 10–11% | 6–7% | 0–3% |

So a radar at the transmitter finds about half the link's trails at 50 km and a tenth at
300 km, by the specular condition alone. Warped long-lived trails would add to this and are not
modelled. An illuminator received at the far end sees all of them at any distance.

Limits: the flux law is continued two decades past where it is established; a single trail
length and height; a steady-path block threshold; the receive gain is taken as available down to
3° elevation, which a flat array does not give.

## Troposcatter as the competing path at short range

Third delegated search, to check my recalled "170 to 180 dB at 300 km". Same tags.

- **No ITU troposcatter formula is validated at 50 MHz.** The older P.617 climate method is
  based on 200 MHz to 4 GHz; P.452 starts at about 0.1 GHz; P.617-6 says only "greater than
  30 MHz"; P.2001 states 30 MHz to 50 GHz; P.1546 covers 30–4,000 MHz by extrapolating below
  100 MHz [read].
- **No measured 50 MHz troposcatter path-loss data for 150–500 km was found.**
- Median basic transmission loss between isotropic antennas, 50 MHz, smooth land, antennas 10 m
  high [calc by the research agent from the formulas it read]:

  | Method | 150 km | 300 km | 500 km |
  | --- | ---: | ---: | ---: |
  | P.617-6 (2025) | 167.6 dB | 183.2 | 194.7 |
  | P.617-3, climate zone 6 | 155.4 | 170.6 | 184.3 |
  | P.2001-6, zone 6 | 156.2 | 172.4 | 190.3 |
  | P.452-18 | 154.2 | 170.4 | 188.3 |
  | NBS TN101 approximation, without its low-antenna term | 154.2 | 170.0 | 186.6 |
  | P.1546 land curves, extrapolated to 50 MHz | 166.3 | 179.9 | 196.6 |

- The low figures leave out the loss of low antennas, which NBS TN101 says they underestimate
  at low frequencies and low heights. The P.1546 curves and P.617-6 are the safer planning
  values [read]. My recalled 170 to 180 dB at 300 km was about right.
- Loss not exceeded for 99% of the time: about 15–18 dB above the median at 150 km and 10–13 dB
  at 500 km (P.617-6: 183.3 / 197.1 / 205.0 dB) [calc].
- Fading: approximately Rayleigh, "a few fades per minute at lower frequencies" [read,
  P.617-6]; hourly medians log-normal with 4–8 dB standard deviation. Implied Doppler spread at
  50 MHz of order 0.01–0.1 Hz [calc, no direct measurement].
- Amateur practice: 50 MHz troposcatter works but appears weak, mainly because antennas are low
  in wavelengths [read, OZ1RH].

What 100 mW gets, median, galactic noise (−161.1 dBm/Hz), no antenna gain [calc]:

| Distance | Loss range | C/N₀ | 10-bit block by section 10 of the specification |
| ---: | --- | ---: | --- |
| 150 km | 154–168 dB | +13 to +27 dB-Hz | seconds |
| 300 km | 170–183 dB | −2 to +11 dB-Hz | 15 s to about 20 min |
| 500 km | 187–197 dB | −16 to −6 dB-Hz | hours to days |

2 mW is 17 dB lower. For 99% availability take off a further 10 to 18 dB.

Against meteor trails with no antenna gain (about two block-worth trails an hour from 50 to
500 km):

- **Up to about 300 km troposcatter is the better path** for 100 mW: continuous, inside the
  specification's range, and served by the specified modem as it stands.
- **From about 400 km meteor trails win**, because troposcatter loss grows about 10 dB per
  100 km and the meteor figures hardly change.
- **At 2 mW troposcatter still works at 150 km** (about −4 to +10 dB-Hz) where meteor trails
  give nothing without an array.
- **Troposcatter at 50 MHz is a slow-fading path** (0.01–0.1 Hz). The one-cell tone is the slow
  choice there, by the 12 to 14 dB of time the specification records for 0.1 Hz and below; a
  held tone of seconds to tens of seconds fits.
- **Footprint.** Troposcatter covers a wide area, so anyone within range receives it; a meteor
  trail serves a strip of about 5 × 40 km. Where concealment by geography matters, that
  favours the trail.

Sources: ITU-R Recommendations P.617-6 (2025), P.617-4, -3 and -1, P.452-18 (2023), P.2001-6
(2025), P.1546-6 (2019, tabulated values taken from a third-party copy of the P.1546-2
spreadsheet), P.526-15 — all at <https://www.itu.int/rec/R-REC-P> and read; Rice, Longley,
Norton and Barsis, "Transmission loss predictions for tropospheric communication circuits",
NBS Technical Note 101, volume 1 (1965, revised 1967),
<https://its.ntia.gov/publications/download/TN101v1.pdf> — read (DOIs 10.6028/nbs.tn.101-1 and
10.70220/4cb052v8 appeared in Crossref results but were not resolved individually);
Preben-Hansen (OZ1RH), "Troposcatter inside and out",
<https://www.qsl.net/oz1rh/troposcatter99/troposcatter99.htm> — read. Not obtained: CCIR Report
238, the TN101 low-antenna curves, any measured 50 MHz data set.

## Open

- **Formulas.** The overdense formulas were read only in secondary reproductions; Sugar 1964,
  Hines and Forsyth 1957 and McKinley 1961 remain unread.
- **Measurements.** No measured distribution was found of overdense durations beyond 1 s, of
  Doppler spread on trails, of overdense rates per hour, or of 50 MHz troposcatter loss.
- **Flux.** Whether trails above q continue to fall as 1/q up to 10¹⁶, and the sign of the
  P.843 diurnal factor. Shower against sporadic rates.
- **Whole-sky model.** Trail length and height distributions, fading inside an echo, the gain a
  real array keeps at low elevation, and non-specular echoes from warped trails (which would
  raise both the link's and a radar's share).
- **The modem on this path.** Block time for a 10-bit block collected over one or several
  trails; the cell length that suits 1–10 Hz fading; the per-trail frequency search; the
  evidence clip.
- **Troposcatter at 50 MHz.** A prediction method validated there, the low-antenna loss, and
  the fading rate.
- **Other frequencies.** Only 50 MHz was computed for overdense and whole-sky cases; 40 MHz
  should be better by the λ laws.

## References

DOIs marked "verified" returned matching metadata from Crossref during this research.

- ITU-R Recommendation P.843-1 (1997), "Communication by meteor-burst propagation".
  <https://www.itu.int/rec/R-REC-P.843> — read.
- ITU-R Recommendation P.372-14 (2019), "Radio noise". <https://www.itu.int/rec/R-REC-P.372> —
  read.
- Sugar, G. R. (1964), "Radio propagation by reflection from meteor trails", Proceedings of the
  IEEE 52, 116–136. DOI 10.1109/PROC.1964.2801 — verified; text not read.
- Weitzen, J. A. and Ralston, W. T. (1988), "Meteor scatter: an overview", IEEE Transactions on
  Antennas and Propagation 36(12), 1813–1819. DOI 10.1109/8.14403 — verified; text not read.
- Forsyth, P. A., Vogan, E. L., Hansen, D. R. and Hines, C. O. (1957), "The Principles of
  JANET—A Meteor-Burst Communication System", Proceedings of the IRE 45, 1642–1657.
  DOI 10.1109/JRPROC.1957.278296 — verified; not read.
- Bartholomé, P. J. and Vogt, I. M. (1968), "COMET—A New Meteor-Burst System Incorporating ARQ
  and Diversity Reception", IEEE Transactions on Communication Technology 16, 268–278.
  DOI 10.1109/TCOM.1968.1089833 — verified; not read.
- Oetting, J. D. (1980), "An Analysis of Meteor Burst Communications for Military Applications",
  IEEE Transactions on Communications 28, 1591–1601. DOI 10.1109/TCOM.1980.1094849 — verified;
  read only as quoted in NTIA 83-116.
- Weitzen, J. A., Sowa, M. J., Scofidio, R. A. and Quinn, J. (1987), "Characterizing the
  Multipath and Doppler Spreads of the High-Latitude Meteor Burst Communication Channel", IEEE
  Transactions on Communications 35, 1050–1058. DOI 10.1109/TCOM.1987.1096690 — verified; not
  read.
- Milstein, L. B. et al. (1987), "Performance of Meteor-Burst Communication Channels", IEEE
  Journal on Selected Areas in Communications 5, 146–154. DOI 10.1109/JSAC.1987.1146515 —
  verified; not read.
- Abel, M. W. (1986), "Meteor Burst Communications: Bits per Burst Performance Bounds", IEEE
  Transactions on Communications 34, 927–936. DOI 10.1109/TCOM.1986.1096638 — verified; not
  read.
- Haakinson, E. J. (1983), "Meteor Burst Communications Model", NTIA Report 83-116.
  <https://www.its.ntia.gov/publications/download/83-116.pdf> — read. DOI 10.70220/8s7g8n15
  found in Crossref.
- Cohen, D., Grant, W. and Steele, F. (1989), "Meteor Burst System Communications
  Compatibility", NTIA Report 89-241.
  <https://www.ntia.gov/report/1989/meteor-burst-system-communications-compatibility> — read.
  DOI 10.70220/0r705f5c found in Crossref.
- Spann, M. D. (1987), "An Adaptive Protocol for Use Over Meteor Scatter Channels", PhD thesis,
  University of Natal.
  <https://researchspace.ukzn.ac.za/bitstreams/c273a27c-7686-41e4-bbfe-7f55879c708c/download> —
  read.
- Franke, S. J. and Taylor, J. H. (2017), "The MSK144 Protocol for Meteor-Scatter
  Communication", QEX, September/October 2017.
  <https://www.arrl.org/files/file/QEX_Next_Issue/SeptOct2017/FrankeTaylor.pdf> — read.
- Warren, D. E. and Desourdis, R., "Meteor Burst Communications: The Gap Filler".
  <https://www.qsl.net/w8wn/hscw/papers/metburdu.txt> — read.
- BRAMS project. <https://brams.aeronomie.be/> — read. GB3MBA beacon.
  <https://ukmeteorbeacon.org/Tx> — search snippet only.
- Morin, S. (1987), MITRE modulation comparison, DTIC ADA196685 — abstract snippet only.
- Thummala, Flores and Falco (2026), "VHF Reconfigurable Intelligent Surfaces for Meteor Burst
  Communication", arXiv:2609.38602 — read (overdense formula, eq. 7; 15 s cap). The identifier
  is as reported by the research agent and was not checked by me.
- Schilling, D. L. and Hibshoosh, E. (1988), "Communications Using Channels Formed by Meteor
  Bursts", AFOSR report, DTIC ADA209856 (archive.org mirror) — read.
- Ostergaard, J. (1990), "Meteor Scatter Communication Between Thule and Station Nord,
  Greenland", GL-TR-90-0248, DTIC ADA234440 — read.
- Weitzen, J. A. (1986), "The Multipath and Fading Profile of the High Latitude Meteor Burst
  Communication Channel", RADC-TR-86-166, DTIC ADA174718. DOI 10.21236/ADA174718 — verified;
  read.
- Ye, Q., Brown, P. G., Campbell-Brown, M. D. and Weryk, R. J. (2013), Monthly Notices of the
  Royal Astronomical Society 436, 675. DOI 10.1093/mnras/stt1605 — verified; read (arXiv
  1304.7841).
- Ye, Q., Wiegert, P. A., Brown, P. G., Campbell-Brown, M. D. and Weryk, R. J. (2014), Monthly
  Notices of the Royal Astronomical Society 437, 3812. DOI 10.1093/mnras/stt2178 — verified;
  read (arXiv 1311.1733).
- Šimek, M. (1992), "Diurnal variation of overdense meteor echo duration and ozone", Asteroids,
  Comets, Meteors 1991, 557–560, NASA NTRS 19930010054 — read.
- Foschini, L. (1998), "On the interaction of radio waves with meteoric plasma",
  arXiv:astro-ph/9804279 — read.
- Berry, J. A. (1959), "Investigation of Certain Aspects of Meteor-Burst Propagation", MS
  thesis, Georgia Institute of Technology — read.
- Williams (1992), "Feasibility of Meteor Burst Buoy Relay", Naval Postgraduate School thesis,
  DTIC ADA248364 — read (source of the Schanker 1990 figures).
- National Bureau of Standards Technical Note 86, "The NBS Meteor-Burst Propagation Project: A
  Progress Report". <https://nvlpubs.nist.gov/nistpubs/Legacy/TN/nbstechnicalnote86.pdf> — read.
- International Meteor Organization, "Theory of Meteor Reflection".
  <https://www.imo.net/observations/methods/radio-observation/reflection/> — read.
- Weitzen, J. A. (1990), "A study of the ground illumination footprint of meteor scatter
  communication", IEEE Transactions on Communications 38, 426–430. DOI 10.1109/26.52652 —
  verified; abstract only.
- Hines, C. O. and Forsyth, P. A. (1957), "The forward-scattering of radio waves from overdense
  meteor trails", Canadian Journal of Physics 35, 1033–1041. DOI 10.1139/p57-115 — verified;
  not read.
- Akram, F., Sheikh, N. M., Javed, A. and Grossi, M. D. (1977), IEEE Transactions on
  Communications 25, 467–470. DOI 10.1109/TCOM.1977.1093831 — verified; not read.
- Baggaley, W. J. and Cummack, C. H. (1974), Journal of Atmospheric and Terrestrial Physics 36,
  1759–1773. DOI 10.1016/0021-9169(74)90161-5 — verified; not read.
- Poulter, E. M. and Baggaley, W. J. (1977), Journal of Atmospheric and Terrestrial Physics 39,
  757–768. DOI 10.1016/0021-9169(77)90137-4 — verified; not read.
- Jones, J., McIntosh, B. A. and Šimek, M. (1990), Journal of Atmospheric and Terrestrial
  Physics 52, 253–258. DOI 10.1016/0021-9169(90)90092-2 — verified; not read.
- Jones, J. and Šimek, M. (1995), Earth, Moon and Planets 68, 329–338.
  DOI 10.1007/BF00671524 — verified; not read.
- McIntosh, B. A. (1968), IAU Symposium 33, 343–351. DOI 10.1017/S0074180900019896 — verified;
  not read.
- Kokjer, K. J. and Roberts, T. D. (1986), IEEE Communications Magazine 24(11), 23–29.
  DOI 10.1109/MCOM.1986.1092982 — verified; not read.
- No DOI found: Schanker 1990 ("Meteor Burst Communications", Artech House), Kaiser and Closs
  1952, Carpenter and Ochs 1962.
- McKinley, D. W. R. (1961), "Meteor Science and Engineering", McGraw-Hill — not accessed.
- Not reachable: DTIC ADA184433 and ADA209856, the likely sources of the COMET, SNOTEL and
  Alaska system parameter tables.

# Meteor burst as a path for the Matrix Modem FSK

Research note of 27 November 2026. It replaces the unsourced meteor-burst remarks made earlier
the same day (recorded in [the lattice design note](fsk-lattice-design.md)). The calculation is
[`meteor_burst.cpp`](../../../_appraisal/algorithms-matrixModem/meteor-burst/meteor_burst.cpp)
with [its results](../../../_appraisal/algorithms-matrixModem/meteor-burst/results/meteor_burst.md).

Tags: **[read]** read in the source during this research; **[calc]** computed here from those
formulas; **[recalled]** remembered, not confirmed in a source. The literature search was done
by a research subagent; I have not re-read the sources myself.

## Conclusions

1. **At 2–100 mW with small antennas meteor burst is not a usable path.** The strongest
   underdense trail (10¹⁴ electrons/m) gives a peak of −6 dB-Hz at 100 mW and −23 dB-Hz at 2 mW
   between isotropic antennas at 50 MHz over 1,300 km [calc]. No underdense trail delivers even
   one tone's worth of energy.
2. **My earlier estimate was wrong.** I had said +10 to +30 dB-Hz at the burst peak for 2–100 mW
   with 160 to 180 dB of loss. The loss for the strongest underdense class is 176 to 188 dB at
   50 MHz, and typical trails are 10 to 30 dB weaker. +10 to +30 dB-Hz needs about 10 to 17 dBi
   at each end at 100 mW.
3. **With directional antennas at both ends 100 mW becomes marginal.** At 17 dBi each end,
   23 to 73 bursts an hour carry one tone's worth of energy; at 10 dBi each end, 0 to 15. At
   2 mW only the 17 dBi case with the most favourable decay time gives any (10 an hour) [calc].
4. **No published result at 10 W or less was found.** Amateur guidance calls 50–100 W
   reasonable and says the mode is not suited to very low power [read]. Operational systems use
   300 W to 2 kW.
5. **The channel during a burst is clean but brief.** Delay spread is under 100 ns for 90% of
   the time, the coherence bandwidth is hundreds of kilohertz or more, and the Doppler shift is
   about 20 Hz at 40 MHz [read]. Half of the bursts on the measured link lasted under 150 ms.
6. **The modulation question is settled by energy, not by the channel.** The path would carry
   PSK, and high-power systems use it. At these powers no burst holds enough energy for a
   carrier, each burst has a new phase and a frequency offset of tens of hertz, and a
   noncoherent tone about as long as the burst is the fitting signal.
7. **Three settings of the specification do not fit.** The 20 Hz shift exceeds the 6.25 Hz
   position spacing, so a per-burst frequency search over several positions is needed. Median
   bursts are shorter than the 160 ms cell. The evidence clip caps a strong tone.

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

- Meteor burst is outside the specification's power range with small antennas, and should not
  be listed as a supported path. With directional antennas at both ends and 100 mW it is a
  marginal erasure channel of tens of tones an hour.
- If it were pursued, the fitting signal is still a noncoherent keyed tone per burst. It would
  need a per-burst frequency search of about ±30 Hz at 50 MHz, a cell near 40–80 ms for the
  median burst, the evidence clip raised, and a block collected over many bursts.
- 40 MHz is better than 50 MHz by about a factor of two in throughput. 144 MHz is 14 dB worse in
  loss, with bursts an eighth as long.

## Open

- Overdense trails, which are the only ones strong enough at these powers with small antennas:
  their rate and level were not computed.
- The sky-coverage gain of wide antennas against the level they lose.
- Block time for a 10-bit block collected over bursts.
- The sign of the P.843 diurnal factor.

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
- McKinley, D. W. R. (1961), "Meteor Science and Engineering", McGraw-Hill — not accessed.
- Not reachable: DTIC ADA184433 and ADA209856, the likely sources of the COMET, SNOTEL and
  Alaska system parameter tables.

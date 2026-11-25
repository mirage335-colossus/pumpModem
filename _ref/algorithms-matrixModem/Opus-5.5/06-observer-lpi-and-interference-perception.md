# Observer detectability and perceived interference

Research date: 24 November 2026. The user's question is whether the
transmission is recognizable as "not background noise" at the receiver's
location and beyond, by someone without the key, and how that compares with
the time the intended receiver needs. This chapter gives model predictions
from `lpi_observer.cpp` and the theory review, and a protocol for the
experimental measurement the user asked for. It extends
[Sol-6.2's observer chapter](../Sol-6.2/observer-and-interference.md) and the
repository's [energy-detector estimates](../../../docs/lpi-estimates.md).

## Findings

1. **A radiometer is defeated by noise-level uncertainty at these signal levels.**
   With noise calibration uncertain by only ±0.1 dB, a band radiometer cannot
   detect a continuous signal whose in-band SNR is below about −13 dB at any
   observation time (Tandra–Sahai SNR wall); for a 2.2 kHz band that is
   C/N0 ≈ +20 dB-Hz. All weak-link operating points in this research are far
   below it. Real HF noise varies by several dB over minutes, so practical
   radiometers are even weaker.
2. **The real observer is a sparsity detector.** An observer who knows the
   public waveform grid (symbol duration and tone or tile spacing) but not the
   key can compare cells within each symbol interval, which is immune to noise
   level. At low per-cell SNR it needs far longer than the intended receiver;
   at efficient per-cell SNR it does not.
3. **Energy efficiency and low observability pull in opposite directions for
   plain tones.** Noncoherent tone signaling is most efficient at about
   6–10 dB Es/N0 per symbol. At +6 dB per symbol a clairvoyant unkeyed observer
   detects presence (P_fa 10⁻³, P_d 0.9) in about 30 symbol intervals, about
   half the time a 128-bit message takes at the same per-symbol SNR.
4. **Keyed spreading within each tile resolves the tension.** If each symbol is
   a keyed noise-like waveform spanning G complex dimensions (bandwidth W within
   the coherence bandwidth, W·T = G), the keyed receiver despreads with no
   energy loss, while the observer's best statistic sees the energy spread over
   G dimensions. At +6 dB per symbol, G = 16 raises the noise-invariant
   observer's time to about 3 message durations and G = 64 to about 17; at
   +3 dB per symbol, about 23 and 140.
5. **Frequency hopping makes long-term spectra flat.** A keyed hop over M
   positions spreads average power uniformly, so a time-averaged waterfall shows
   only an elevation of ρ/M per cell, far below visual or radiometric detection.
   A fixed tone set would instead accumulate a visible line.
6. **The public amateur key provides no concealment.** With a published key, an
   observer is a keyed receiver. Low observability exists only in a secret-key
   mode, whose regulatory status in amateur bands is restrictive (see the
   [keys chapter](09-keys-schedules-spectrum-and-stream-contract.md)).

## Model

Each symbol interval holds M_obs tiles. One tile carries the symbol with
Rayleigh-faded energy of mean Es (per-symbol Es/N0 = ρ); inside the tile the
waveform spans G complex dimensions. G = 1 is a plain tone of duration T;
G > 1 is a keyed noise-like waveform of bandwidth W = G/T. The total band holds
D = 352 dimensions per interval (2.2 kHz × 160 ms; the results scale with D).
Detection time is computed with a CLT approximation over symbols at
P_fa = 10⁻³ and P_d = 0.9, with paired H0/H1 sampling so small mean shifts
are resolved.

Detectors:

- **D1 radiometer:** total energy, noise level known exactly (analytic).
- **D2 clairvoyant LRT:** exact per-symbol likelihood ratio for "one of M_obs
  tiles is occupied", knowing ρ and the noise level (an upper bound on what an
  unkeyed observer can do with per-symbol statistics).
- **D3 self-normalized LRT:** the same after dividing tile energies by their
  per-symbol mean, so the null distribution is independent of the noise level.
- **Keyed presence:** the intended receiver's presence test over 64 candidate
  despread dimensions with the data unknown.
- **msg128:** symbols needed for 128 information bits at the ideal symbol-wise
  mutual information of 64-ary noncoherent orthogonal signaling.

### Results (symbols of observation)

| Es/N0 per symbol | G | M_obs | D1 radiometer | D2 clairvoyant | D3 noise-invariant | Keyed presence | msg128 | D3 / msg128 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| −10 dB | 1 | 352 | 6.7×10⁵ | 6.3×10⁵ | 5.8×10⁷ | 1.2×10⁵ | 1.7×10⁴ | 3,300 |
| −3 dB | 1 | 352 | 2.7×10⁴ | 2.0×10⁴ | 8.6×10⁴ | 3.8×10³ | 934 | 92 |
| −3 dB | 16 | 22 | 2.7×10⁴ | 2.6×10⁴ | 1.6×10⁶ | 3.8×10³ | 934 | 1,750 |
| 0 dB | 1 | 352 | 6.7×10³ | 2.2×10³ | 3.6×10³ | 572 | 302 | 12 |
| 0 dB | 16 | 22 | 6.7×10³ | 6.0×10³ | 6.1×10⁴ | 572 | 302 | 200 |
| 0 dB | 64 | 5 | 6.1×10³ | 6.0×10³ | 2.7×10⁵ | 572 | 302 | 900 |
| +3 dB | 1 | 352 | 1.7×10³ | 190 | 218 | 83 | 116 | 1.9 |
| +3 dB | 16 | 22 | 1.7×10³ | 980 | 2.7×10³ | 83 | 116 | 23 |
| +3 dB | 64 | 5 | 1.6×10³ | 1.4×10³ | 1.6×10⁴ | 83 | 116 | 137 |
| +6 dB | 1 | 352 | 433 | 30 | 31 | 19 | 58 | 0.53 |
| +6 dB | 16 | 22 | 433 | 120 | 168 | 19 | 58 | 2.9 |
| +6 dB | 64 | 5 | 394 | 264 | 962 | 19 | 58 | 17 |
| +10 dB | 1 | 352 | 74 | 6.8 | 6.6 | 5.5 | 33 | 0.20 |
| +10 dB | 64 | 5 | 68 | 31 | 48 | 5.5 | 33 | 1.4 |

Where the self-normalized observer's mean shift was not resolved by 4×10⁵
simulated symbols (low SNR with G ≥ 4), its time exceeds about 10⁶ symbols;
those rows are omitted. The full table is `calc/results/lpi_observer.md`.

The theory review's independent estimate agrees: at Es/N0 ≈ 6 dB per symbol an
unkeyed observer that knows timing and the tone grid gathers about 0.4 nats of
discrimination per symbol, i.e. detection in roughly 16 symbols; at low
per-symbol SNR it behaves like a radiometer, needing T ≈ d²B/(C/N0)².

### Radiometer SNR wall

| Noise-level uncertainty ±u | Wall in-band SNR | Equivalent C/N0 (2.2 kHz) | Per-symbol Es/N0 at 160 ms |
| ---: | ---: | ---: | ---: |
| 0.10 dB | −13.4 dB | +20.1 dB-Hz | +12.1 dB |
| 0.25 dB | −9.4 dB | +24.0 dB-Hz | +16.1 dB |
| 0.50 dB | −6.4 dB | +27.1 dB-Hz | +19.1 dB |
| 1.00 dB | −3.3 dB | +30.1 dB-Hz | +22.1 dB |

## Interpretation for Matrix Modem

- **"Perceived interference" at the receive site** is not an issue for the
  weak-link operating points: at +10 dB-Hz the in-band SNR in a 2.2 kHz SSB
  channel is about −23 dB (inaudible), and the wall table shows radiometric
  detection is impossible with realistic noise uncertainty. What a human or
  software observer could notice is *structure*: narrow persistent lines
  (prevented by hopping) and strong individual cells on a high-resolution
  waterfall (limited by keeping per-cell SNR low).
- **Long symbols concentrate energy per cell.** The capacity analysis favours
  Es/N0 of 6–10 dB per symbol. With plain tones (G = 1) that is detectable by a
  knowledgeable observer within about one message. Keyed spreading (family B in
  the [design chapter](05-waveform-and-parameter-design.md)) keeps the receiver's
  per-symbol energy while lowering the observer's per-dimension SNR. G is
  limited by the coherence bandwidth (W ≲ 10–50 Hz for few-ms delays) times T,
  so long symbols also allow larger G.
- **Covert-communication theory sets the long-run limit.** For a same-channel
  observer with exact statistics, information hidden within a fixed
  distinguishability grows only as √n (square-root law; Bash, Goeckel & Towsley
  2013; Wang, Wornell & Zheng 2016 give covert information ≈ √(nδ) nats with a
  shared key). Noise uncertainty can allow linear scaling in some models, but
  multi-antenna or continuous-time observers can restore the square-root law
  (references in [Sol-6.2](../Sol-6.2/observer-and-interference.md)). Matrix
  should therefore treat low observability as a time-bounded property measured
  against named observer classes, not as a guarantee.
- **Helper tones and any fixed structure are observable features.** A Helper
  pilot at a fixed frequency accumulates a line; its power and duty must be
  included in the observer evaluation.

## Experimental protocol (the user's requested measurement)

1. Fix observer classes: wideband radiometer; channelized radiometer over the
   public grid; noise-invariant sparsity detector (D3-like) with the public
   T and spacing; and, for public-key mode, the keyed receiver itself.
2. Use captured HF noise from the receive site (or representative captures) for
   both H0 and H1; add the simulated received signal after the channel model so
   each noise sample is shared by the paired arms.
3. For each configuration, report observer detection time at predeclared
   P_fa per hour and P_d, alongside the intended receiver's complete-message
   time at the same energy; report their ratio, as in the table above.
4. Split episodes into calibration and evaluation sets by time/site so the
   observers' thresholds are not tuned on the evaluation noise.
5. Treat the result as the ratio for the tested observers, not as a bound on all
   observers.

## Sources

- Tandra, R., Sahai, A. "SNR Walls for Signal Detection." IEEE JSTSP 2(1), 2008, doi:10.1109/JSTSP.2007.914879.
- Bash, B. A., Goeckel, D., Towsley, D. "Limits of Reliable Communication with Low Probability of Detection on AWGN Channels." IEEE JSAC 31(9), 2013, doi:10.1109/JSAC.2013.130923.
- Wang, L., Wornell, G. W., Zheng, L. "Fundamental Limits of Communication With Low Probability of Detection." IEEE TIT 62(6), 2016 (as summarized in the theory review).
- Further covert-communication qualifications (noise uncertainty, multi-antenna and continuous-time observers) with access notes: [Sol-6.2 observer chapter](../Sol-6.2/observer-and-interference.md).
- Calculations: `calc/lpi_observer.cpp`, `calc/results/lpi_observer.md`.

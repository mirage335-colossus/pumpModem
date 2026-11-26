# Information limits and noncoherent penalties

Research date: 24 November 2026. This chapter estimates what any one-way
receiver can achieve before an implementation is chosen, and identifies which
losses are fundamental. It combines a literature review made for this pass with
independent calculations in
[`_appraisal/algorithms-matrixModem/calc`](../../../_appraisal/algorithms-matrixModem/calc/README.md).
The two agree closely where they overlap.

## Findings

1. **One number governs weak noncoherent reception: the energy received per
   fading coherence interval.** Let x = (C/N0)·T_c ≈ (C/N0)/B_eff, where
   B_eff = (∫S)²/∫S² is the effective Doppler bandwidth (for a Gaussian Doppler
   spectrum with F.1487 spread 2σ, B_eff = 2√π σ ≈ 1.77·(2σ)). Around x ≳ 20 dB
   noncoherent reception is within a few tenths of a dB of AWGN; at x ≈ 0 dB it
   loses 5–7 dB; for x ≪ 1 the rate collapses to R ≈ (C/N0)²/(2B_eff) nats/s,
   so every dB of C/N0 lost costs two dB of rate.
2. **For a peak-limited transmitter this is a capacity, not a modem defect.**
   For frequency-flat Rayleigh fading with Doppler spectrum S_H, the
   peak-limited capacity is C = C/N0 − ∫ln(1 + (C/N0)·S_H(f))df nats/s
   (Sethuraman & Hajek 2005, Prop. 3.3; Viterbi 1967 and Durisi, Bölcskei &
   Shamai 2007 for constant-envelope FSK achieving the same expression; the
   identification of the two is an inference flagged in the review). This
   pass's independent Monte Carlo of noncoherent MFSK with long symbols and
   Doppler-matched subspace detection reaches **97–99.8% of that bound
   throughout the low-C/N0 regime** (table below). In that regime no waveform
   family — DSSS, OFDM, chirp, OTFS, multi-carrier joint detection — can do
   materially better under the same assumptions. The assumptions matter:
   Rayleigh (no specular component), a smooth Gaussian Doppler spectrum, one
   receive antenna, no transmitter peakiness.
3. **Unknown phase alone is nearly free.** If the phase is constant over the
   message, one-shot ML detection of 2^k orthogonal waveforms costs only
   0.1–0.5 dB more than coherent detection for 16–128-bit messages. The large
   penalties come from channel *variation* (Doppler spread, fading) and from
   processing that throws away phase continuity, not from not knowing the phase.
4. **Short symbols at low C/N0 are the dominant avoidable loss.** With a fixed
   160 ms symbol, the AWGN baseline needs Eb/N0 ≈ 19 dB at −10 dB-Hz,
   12 dB at −2 dB-Hz and 5 dB at +6 dB-Hz, because each symbol carries too
   little energy. Spread-fading requirements vary and can be higher: the
   30 Hz row needs 28.9, 21.0 and 13.2 dB at those same C/N0 values. The best
   symbol duration grows as C/N0 falls
   (seconds to tens of seconds). Long durations can be near-optimal in the
   idealized duration-optimized sweeps, but a particular 2.56–10.24 s choice
   is not within 0–3 dB across all models: at −10 dB-Hz, 2.56 s costs 8.5 dB
   in AWGN and 7.3 dB in slow Rayleigh, while 10.24 s costs 3.8 dB in AWGN.
   These are penalties against each model's best duration at the same C/N0,
   not absolute Eb/N0 values or a guarantee over a continuous C/N0/Doppler
   range. See the [25 November qualification](README.md#dated-qualifications-25-november-2026),
   the [information-limits review](../Sol-6.2/information-limits-review.md#boundaries-for-waveform-decisions),
   and the [finite-alphabet fading study](../Sol-6.2/fading-limit-and-mfsk-alternatives.md#finite-alphabet-in-a-22-khz-band).
5. **Fading needs time diversity; outages need message-scale erasure tolerance.**
   With no channel knowledge, 100–128-bit messages at 10⁻³ need about
   3.8–4.3 dB Eb/N0 and roughly 30–100 independent fades (Yang, Durisi &
   Polyanskiy 2016), versus about 0.3 dB in AWGN, and about 28 dB if the message
   sits inside a single fade. On quiet HF paths with 20–200 s fade cycles, that
   diversity takes tens of minutes to hours to accumulate.
6. **Search breadth costs sensitivity, slowly.** Each factor of 1,000 in
   hypotheses raises the detection threshold by about 1–2 dB for single-look
   detection (less with longer noncoherent integration), and about 0.3 dB per
   factor of 2^10 in an ideal joint detector. Compute should be spent on
   hypotheses that lengthen coherent integration or remove sync overhead, not
   on breadth for its own sake.
7. **Four-bit arithmetic is nearly lossless here.** Input, template and
   intermediate-energy quantization to 16 levels together cost about 0.1–0.2 dB
   ([precision chapter](07-matrix-hardware-and-precision.md)).

## 1. The energy-per-coherence-cell parameter

A constant-envelope tone collects energy E_c = C·T_c during one coherence
interval. If E_c ≫ N0 the receiver can effectively learn the channel from the
signal itself, and noncoherent operation approaches coherent performance. If
E_c ≪ N0 it cannot, and each coherence interval contributes information only
quadratically in its SNR. This is the common thread of the low-SNR noncoherent
literature:

- Sethuraman & Hajek (2005) give the exact peak-limited capacity per unit
  energy, C_p(P) = 1 − ∫log(1 + P·S)/P, achieved asymptotically by bursty
  on-off codewords; for block fading of length T it is 1 − ln(1+PT)/(PT)
  (full text read in the review).
- Telatar & Tse (2000) and Médard & Gallager (2002) show that signals which
  spread power uniformly over many resolvable paths lose mutual information as
  bandwidth grows ("overspreading"), while FSK-like frequency-peaky signals do
  not.
- Ray, Médard & Zheng (2007) show coherent-like wideband capacity needs
  coherence length l ≥ ¼·SNR⁻² symbols, with loss Δ ≈ SNR/(2√l).
- Zheng, Tse & Médard (2007) relate the sublinear term to how coherence time
  scales with SNR.
- Gursoy, Poor & Verdú (2006) show that with a fixed duty factor the minimum
  bit energy occurs at a nonzero rate, and that with only a peak limit the
  minimum Eb/N0 is ln2/[1 − ln(1+η)/η] for Rayleigh fading with η the
  normalized peak energy per symbol — the same η(x) function.

For HF the consequence is direct: a peak-limited transmitter should transmit at
peak continuously and concentrate its energy in frequency (one tone at a time),
and the achievable efficiency is set by x, not by the choice of data rate.

### Rate ceilings

Peak-limited capacity for Rayleigh fading with a Gaussian Doppler spectrum
(`peak_limited_bound.py`; the review's Table A gives the same values and a flat
spectrum variant). Loss relative to AWGN at the same C/N0 in parentheses.

| C/N0 (dB-Hz) | AWGN | 2σ = 0.1 Hz | 1 Hz | 10 Hz | 30 Hz |
| ---: | ---: | ---: | ---: | ---: | ---: |
| −20 | 0.0144 bit/s | 3.9×10⁻⁴ (−15.7 dB) | 4.1×10⁻⁵ (−25.5) | 4.1×10⁻⁶ (−35.5) | 1.4×10⁻⁶ (−40.3) |
| −10 | 0.144 | 0.029 (−7.0) | 3.9×10⁻³ (−15.7) | 4.1×10⁻⁴ (−25.5) | 1.4×10⁻⁴ (−30.3) |
| 0 | 1.44 | 0.91 (−2.0) | 0.29 (−7.0) | 0.039 (−15.7) | 0.013 (−20.3) |
| +10 | 14.4 | 13.1 (−0.4) | 9.1 (−2.0) | 2.9 (−7.0) | 1.2 (−10.8) |

The same numbers as the Eb/N0 each case needs: at −10 dB-Hz, about 5.4 dB
(0.1 Hz), 14.1 dB (1 Hz), 23.9 dB (10 Hz) and 28.7 dB (30 Hz), against
−1.59 dB for AWGN.

### Independent check: noncoherent MFSK reaches the ceiling

`mfsk_capacity.cpp` computes the symbol-wise mutual information of
noncoherent M-ary FSK in a 2.2 kHz band, with the exact estimator-correlator
(Karhunen–Loève weights) for a Gaussian Doppler process, tone spacing
1/T + 4σ, ideal outer coding and ideal interleaving, and maximizes rate over the
symbol duration T. Its estimator uses control variates validated against an
analytic low-SNR expansion.

| 2σ | C/N0 | Peak-limited bound | MFSK best-T | Ratio |
| ---: | ---: | ---: | ---: | ---: |
| 0.1 Hz | −20 / −10 / 0 / +10 dB-Hz | 3.90×10⁻⁴ / 0.0289 / 0.912 / 13.1 bit/s | 3.79×10⁻⁴ / 0.0282 / 0.695 / 7.16 | 0.97 / 0.97 / 0.76 / 0.55 |
| 1 Hz | same | 4.05×10⁻⁵ / 3.90×10⁻³ / 0.289 / 9.12 | 4.04×10⁻⁵ / 3.89×10⁻³ / 0.280 / 6.39 | 1.00 / 1.00 / 0.97 / 0.70 |
| 10 Hz | same | 4.07×10⁻⁶ / 4.05×10⁻⁴ / 0.0390 / 2.89 | 4.04×10⁻⁶ / 4.03×10⁻⁴ / 0.0388 / 2.70 | 0.99 / 0.99 / 0.99 / 0.94 |
| 30 Hz | same | 1.36×10⁻⁶ / 1.36×10⁻⁴ / 0.0134 / 1.19 | 1.34×10⁻⁶ / 1.34×10⁻⁴ / 0.0131 / 1.13 | 0.99 / 0.99 / 0.98 / 0.95 |

The shortfall at high C/N0 and low spread reflects the 2.2 kHz band and finite
alphabet (the bound assumes unlimited bandwidth); the 0.1 Hz case below
−10 dB-Hz is slightly pessimistic because the calculation caps T at 60 s.

### Best symbol duration and the cost of fixed durations

Required Eb/N0 (dB) at the best symbol duration, with the loss relative to
Shannon at the same rate and the optimal T (`results/mfsk_summary.md`):

| Model | −10 dB-Hz | −2 dB-Hz | +6 dB-Hz | +14 dB-Hz | +20 dB-Hz |
| --- | ---: | ---: | ---: | ---: | ---: |
| Stable phase (AWGN, unknown phase) | −0.5 [1.1] (90 s) | −0.3 [1.3] (14 s) | 0.0 [1.6] (2 s) | 0.4 [1.9] (0.25 s) | 0.9 [2.4] (0.05 s) |
| Slow Rayleigh (ergodic) | 0.7 [2.3] (63 s) | 0.9 [2.5] (8 s) | 1.3 [2.9] (1.4 s) | 1.7 [3.3] (0.16 s) | 2.3 [3.8] (0.04 s) |
| 2σ = 0.1 Hz | 5.5 [7.1] (56 s) | 1.8 [3.4] (8 s) | 1.3 [2.9] (0.9 s) | 1.7 [3.3] (0.14 s) | 2.4 [3.9] (0.04 s) |
| 0.5 Hz | 11.3 [12.9] (45 s) | 4.8 [6.4] (10 s) | 1.9 [3.5] (1.4 s) | 1.8 [3.4] (0.16 s) | 2.4 [3.9] (0.03 s) |
| 1 Hz | 14.1 [15.7] (50 s) | 7.0 [8.6] (13 s) | 2.6 [4.2] (1.3 s) | 1.9 [3.5] (0.16 s) | 2.4 [3.9] (0.04 s) |
| 2 Hz | 17.0 [18.6] (25 s) | 9.5 [11.1] (16 s) | 3.9 [5.5] (1.4 s) | 2.1 [3.7] (0.14 s) | 2.4 [4.0] (0.04 s) |
| 5 Hz | 21.0 [22.6] (8 s) | 13.2 [14.7] (8 s) | 6.3 [7.9] (1.1 s) | 2.8 [4.3] (0.11 s) | 2.6 [4.2] (0.03 s) |
| 10 Hz | 24.0 [25.6] (3 s) | 16.1 [17.6] (3 s) | 8.7 [10.3] (2 s) | 3.7 [5.3] (0.11 s) | 2.9 [4.5] (0.03 s) |
| 30 Hz | 28.8 [30.4] (1.3 s) | 20.8 [22.4] (1.3 s) | 13.0 [14.6] (1.3 s) | 6.5 [8.1] (0.16 s) | 3.9 [5.5] (0.03 s) |

Penalty of a *fixed* symbol duration relative to the best duration (dB),
using the modeled tone spacing `Δ = 1/T + 4σ`:

| Fixed T | Model | −10 dB-Hz | −2 dB-Hz | +6 dB-Hz | +14 dB-Hz |
| --- | --- | ---: | ---: | ---: | ---: |
| 0.16 s (1/T = 6.25 Hz) | AWGN | +19.9 | +11.9 | +4.8 | +0.2 |
| | 0.1 Hz | +13.9 | +9.8 | +3.6 | +0.1 |
| | 1 Hz | +5.5 | +4.9 | +2.4 | 0.0 |
| | 10 Hz | +0.5 | +0.5 | +0.4 | +0.1 |
| 2.56 s | AWGN | +8.5 | +2.4 | +0.2 | +6.8 |
| | 0.1 Hz | +2.9 | +0.8 | +0.8 | +6.0 |
| | 1 Hz | +0.3 | +0.2 | +0.4 | +6.2 |
| | 10 Hz | 0.0 | +0.1 | +0.1 | +6.1 |
| 10.24 s | AWGN | +3.8 | +0.1 | +4.5 | +12.1 |
| | 0.1 Hz | +0.5 | +0.1 | +4.0 | +11.3 |
| | 1 Hz | +0.1 | 0.0 | +3.6 | +12.2 |

Here 6.25 Hz is `1/T`, not the modeled tone spacing. With `Δ = 1/T + 4σ`,
modeled `2σ` spreads of 10 and 30 Hz imply 26.25 and 66.25 Hz spacing,
respectively.

Reading: the symbol duration should be chosen from the *design C/N0*, aiming
at roughly 6–10 dB of Es/N0 per symbol in quiet conditions, while the tone
spacing must absorb the Doppler category. The recommendation of about +12 to
+16 dB-Hz for a 160 ms symbol applies to quiet-channel tuning; this is not a
general rejection threshold for that duration. For example, the modeled 10 Hz
spread row has only a 0.5 dB penalty at −10 dB-Hz (with its correspondingly
wider modeled spacing). At low C/N0, the 160 ms default is not a robustness
setting: in disturbed channels a suitably chosen longer symbol with wide spacing is at least as good
(the receiver integrates energy over a low-rank Doppler subspace), and in
quiet channels it is much better. This decoupling is the main departure from
amateur practice, where spacing is tied to 1/T and the FST4 guidance requires
spread below the spacing ([anchors](04-empirical-weak-signal-anchors.md)).

C/N0 (dB-Hz) needed for a target rate at the best duration, against Shannon in
2.2 kHz:

| Model | 0.01 bit/s | 0.1 bit/s | 1 bit/s | 10 bit/s |
| --- | ---: | ---: | ---: | ---: |
| Shannon (2.2 kHz) | −21.6 | −11.6 | −1.6 | +8.4 |
| Stable phase | < −10 | < −10 | −0.3 | +10.1 |
| 0.1 Hz | ≈ −12.5 | −6.6 | +1.5 | +11.6 |
| 1 Hz | −7.9 | −2.6 | +3.5 | +11.9 |
| 10 Hz | −3.0 | +2.1 | +7.5 | +13.8 |
| 30 Hz | −0.6 | +4.5 | +9.7 | +15.6 |

## 2. Finite messages, reliability and diversity

### Energy for k bits on stable channels

Exact error probability of ML detection among M = 2^k orthogonal waveforms in
AWGN (`finite_energy.cpp`; the GEMM-algorithm review independently obtained
the same values):

| k | Coherent, 10⁻² | Coherent, 10⁻³ | One unknown phase, 10⁻³ | Penalty |
| ---: | ---: | ---: | ---: | ---: |
| 8 | 2.53 dB | 3.83 dB | 4.50 dB | 0.68 dB |
| 16 | 1.50 | 2.48 | 2.97 | 0.49 |
| 32 | 0.72 | 1.47 | 1.80 | 0.32 |
| 64 | 0.14 | 0.70 | 0.91 | 0.20 |
| 128 | −0.31 | 0.11 | 0.23 | 0.12 |

The Polyanskiy–Poor–Verdú (2011) minimum-energy expansion gives slightly higher
values at small k (0.31 dB at k = 128) because its O(1) term is dropped;
orthogonal signaling is an achievable reference. At 50% success (the WSJT
convention) 50–77-bit messages need about −1.9 dB coherent and −1.6 dB with
one unknown phase; 99% success costs about 2.2 dB more for k = 50.

### Fading without channel knowledge

Yang, Durisi & Polyanskiy (2016) show that without receiver CSI the gap to
−1.59 dB shrinks only as ((log k)/k)^{1/3}, versus 1/√k with CSI, and give
(ε = 10⁻³, single antenna) achievable Eb/N0 of 8.3 dB at k = 10, 3.8 dB at
k = 100 and 1.1 dB at k = 1000, with optimal numbers of independent looks of
18, 38 and 96. The GEMM-algorithm review reproduced this with square-law
combining of orthogonal codes: at k = 100 and 10⁻³, Eb/N0 is 28.4 dB with one
look, 8.8 dB with 4, 4.3 dB with 16, 3.8 dB with 32 and 4.5 dB with 128. The
theory review's peak-limited variant gives about 4.3 dB for 128 bits with
roughly 100 fades. There is an optimal diversity order: too few looks leaves
outage, too many splits energy below the noncoherent knee.

Fade margin needed when the message energy averages over N_eff independent
Rayleigh fades (`finite_energy.cpp`; ideal energy-averaging code):

| N_eff | ε = 10⁻¹ | 10⁻² | 10⁻³ |
| ---: | ---: | ---: | ---: |
| 1 | 9.8 dB | 20.0 | 30.0 |
| 4 | 3.6 | 6.9 | 9.7 |
| 16 | 1.6 | 2.9 | 4.0 |
| 64 | 0.7 | 1.4 | 1.8 |
| 256 | 0.4 | 0.7 | 0.9 |
| 1024 | 0.2 | 0.3 | 0.4 |

A log-normal slow-fading component that stays constant over a message needs
z_ε·σ dB more (e.g. 9–14 dB at 1% for σ = 4–6 dB). On quiet HF paths, fast
fades recur every 20–200 s and slow fading has 15–60 minute scales
([channel chapter](01-hf-channel-moderate-to-worst.md)), so a message gains
enough independent looks only by spanning tens of minutes to hours, which
low-rate messages do anyway. Short messages on quiet paths are the hardest case
for reliability, not the easiest.

### Pilots versus noncoherent

Östman et al. (2019) found that for a 168-symbol packet over Rayleigh block
fading, optimized pilot-assisted transmission needs about 1.2 dB more energy
than a well-designed noncoherent scheme at 10⁻³ packet error. Together with
finding 3, this argues against dedicating energy to pilots and for joint
detection that treats the codeword (or keyed pattern) as its own reference.

## 3. Search breadth and false alarms

Required total Es/N0 (dB) to detect one candidate among K hypotheses with
family-wise false-alarm probability ≤ 10⁻² (Bonferroni), Pd = 0.5 / 0.9, with
L energy-combined looks (`false_alarm.cpp`):

| K | L = 1 | L = 16 | L = 64 | L = 256 |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 6.1 / 9.4 | 10.5 / 13.0 | 13.1 / 15.4 | 15.9 / 18.0 |
| 10³ | 10.4 / 12.5 | 13.7 / 15.4 | 16.1 / 17.5 | 18.7 / 20.1 |
| 10⁶ | 12.5 / 14.2 | 15.3 / 16.7 | 17.5 / 18.7 | 20.0 / 21.1 |
| 10⁹ | 13.9 / 15.4 | 16.4 / 17.6 | 18.4 / 19.5 | 20.9 / 21.8 |
| 10¹² | 15.0 / 16.3 | 17.2 / 18.3 | 19.1 / 20.1 | 21.5 / 22.4 |

- Going from 10⁶ to 10⁹ hypotheses costs about 1.4 dB with one look and about
  0.9 dB with 64 looks.
- Combining 64 looks by energy costs about 5 dB against coherent combining of
  the same energy; 256 looks about 7.5 dB. This is the classic noncoherent
  integration loss, and it is why lengthening coherent windows is worth many
  orders of magnitude more hypotheses.
- In an ideal joint sync-and-decode detector at k = 100, 2^10, 2^20 and 2^40
  extra cells cost about 0.33, 0.63 and 1.17 dB (GEMM-algorithm review,
  pessimistic because neighbouring cells correlate). Joint detection therefore
  recovers most of a 0.7–3 dB sync-symbol overhead while the search stays below
  roughly 2^20–2^30 cells.
- For continuous frequency scans the number of effectively independent tests
  exceeds the Fourier-bin count: the theory review's Gross–Vitells upcrossing
  estimate gives K_eff ≈ B·T·√(πu/3) at threshold u, about 3–5 times the bin
  count. Empirical whole-search null calibration remains mandatory.

## 4. What is fundamentally ruled out, and what is not

Ruled out under the stated assumptions:

- **Beating the peak-limited fading capacity at low C/N0.** With a Gaussian
  Doppler spectrum and Rayleigh fading, no transmitter waveform or receiver
  computation beats the ceiling table. More receiver compute cannot make a
  10 Hz-spread channel at −10 dB-Hz cost less than about 24 dB Eb/N0.
- **Wideband noise-like signaling filling 2.2 kHz at very low C/N0.** The
  critical bandwidth occupancy of Lozano & Porrat (2012), translated to HF by
  the theory review, is only about 17 Hz at 0 dB-Hz and 170 Hz at +10 dB-Hz
  (B_cT_c ≈ 10³). A continuously wideband waveform is overspread; a
  frequency-peaky signal (one tone or one narrow tile at a time) is not. This
  reconciles the pessimistic overspreading results with optimistic underspread
  results (Durisi, Morgenshtern & Bölcskei 2012), which apply at the high x
  of practical broadband SNRs.
- **Recovering a quasi-static fade or an outage by integration or coding
  within it.** Only time or frequency/route diversity helps.
- **Large gains from pilots, learned receivers or decoders beyond ML.** See
  [the algorithm chapter](08-gemm-receiver-algorithms-and-utilization.md).

Not ruled out, and worth exploiting:

- **Long coherent windows on quiet paths.** The gap between 160 ms symbols and
  optimal durations is 5–20 dB at low C/N0. Exploiting it needs frequency and
  drift search and stable equipment; this is the clearest place where more
  computation buys real sensitivity.
- **Channels better than the Gaussian-Rayleigh model.** The capacity integral
  ∫ln(1 + P·S_H) is much smaller for spectra concentrated in a few narrow lines
  (separate modes with distinct Doppler shifts, specular components) than for a
  single broad Gaussian of the same width. A receiver that resolves and tracks
  discrete modes can do better than the table's 2σ-equivalent row.
- **Receive diversity.** Fading decorrelates within 32 km and between
  polarizations ([channel chapter](01-hf-channel-moderate-to-worst.md)); in the
  square-law regime, L independent receive branches multiply the rate by
  about L. This is outside the single-antenna modem but is not excluded.
- **Energy-optimal power.** Below the knee, total transmit energy falls as power
  rises (rate ∝ power²). If the operating constraint is energy or average power
  rather than a hard peak, higher power for shorter time is better
  ([link budget chapter](02-noise-and-link-budget.md)).
- **Exploiting channel memory across symbols.** Symbol-wise mutual information
  is a lower bound; joint processing across symbols (block detection, sequence
  detection) recovers part of the gap where the channel is coherent across
  symbols. Its value is largest between the AWGN and severe extremes.

## 5. Contradictions and nuances

- *Pessimistic vs optimistic noncoherent theory.* Overspreading results
  (Médard–Gallager, Telatar–Tse, Subramanian–Hajek) and underspread optimism
  (Durisi et al.) are both correct for their x. HF weak-signal operation sits at
  low x unless symbols concentrate energy; broadband wireless intuition does not
  transfer.
- *Flash signaling.* Verdú's −1.59 dB without CSI requires unbounded peakiness;
  Gursoy, Poor & Verdú show that with a fixed duty factor the minimum bit energy
  occurs at a nonzero rate (binary FSK at duty 1: about 6.7 dB; duty 10⁻⁴:
  about −0.2 dB at a peak-to-average ratio of 10⁴). A peak-limited amateur
  transmitter cannot use this; an energy-limited one partly can.
- *Spectral shape.* Gaussian Doppler spectra make the capacity expansion
  well-behaved; Clarke/Jakes spectra make the quadratic bound U_p diverge
  (Sethuraman & Hajek). Real HF spectra are neither; measured or replayed
  channels should test the result.
- *Memory and predictability.* A Gaussian spectrum is mathematically
  "deterministic" (perfectly predictable from the infinite past), a property
  some capacity results exploit but no receiver can, given noise and
  nonstationarity over minutes. Treat results that rely on extreme
  predictability with caution.
- *Convergence in M.* AWGN noncoherent MFSK approaches its limit slowly: the
  theory review computed minimum Eb/N0 of 0.16 dB at M = 1024 and 0.04 dB at
  M = 4096, the empirical review 1.1 dB at M = 64 and about 0.5 dB at M = 256;
  with one Rayleigh fade per symbol, about 1.4 dB at M = 1024 against a 0.65 dB
  infinite-M bound.

## Sources

Access notes: **full** = full text read in this pass's reviews; **abstract**;
**secondary** = through a citing source.

- Sethuraman, V., Hajek, B. "Capacity per unit energy of fading channels with a peak constraint." IEEE TIT 51(9), 2005 (full).
- Viterbi, A. J. "Performance of an M-ary orthogonal communication system using stationary stochastic signals." IEEE TIT 13(3), 1967, doi:10.1109/TIT.1967.1054039 (abstract; restated by Durisi et al.).
- Durisi, G., Bölcskei, H., Shamai, S. "Capacity of underspread noncoherent WSSUS fading channels under peak signal constraints." arXiv:0705.1682, ISIT 2007 (full).
- Durisi, G., Morgenshtern, V. I., Bölcskei, H. "On the sensitivity of continuous-time noncoherent fading channel capacity." IEEE TIT 58(10), 2012, doi:10.1109/TIT.2012.2207089 (reviewed in [Sol-6.2](../Sol-6.2/information-limits.md)).
- Telatar, İ. E., Tse, D. N. C. "Capacity and mutual information of wideband multipath fading channels." IEEE TIT 46(4), 2000 (full).
- Médard, M., Gallager, R. G. "Bandwidth scaling for fading multipath channels." IEEE TIT 48(4), 2002, doi:10.1109/18.992769 (secondary).
- Subramanian, V. G., Hajek, B. "Broad-band fading channels: signal burstiness and capacity." IEEE TIT 48(4), 2002 (abstract).
- Verdú, S. "Spectral efficiency in the wideband regime." IEEE TIT 48(6), 2002, doi:10.1109/TIT.2002.1003824 (secondary).
- Gursoy, M. C., Poor, H. V., Verdú, S. "On-off frequency-shift keying for wideband fading channels." EURASIP JWCN 2006; arXiv:cs/0510075 (full).
- Ray, S., Médard, M., Zheng, L. "On noncoherent MIMO channels in the wideband regime: capacity and reliability." IEEE TIT 2007; arXiv:cs/0603018 (full).
- Zheng, L., Tse, D. N. C., Médard, M. "Channel coherence in the low-SNR regime." IEEE TIT 53(3), 2007, doi:10.1109/TIT.2006.890777 (abstract/secondary).
- Lozano, A., Porrat, D. "Non-peaky signals in wideband fading channels: achievable bit rates and optimal bandwidth." IEEE TWC 11(1), 2012 (abstract; formulas via Gómez-Cuba et al., arXiv:1501.04905, full).
- Porrat, D., Tse, D. N. C., Nacu, Ş. "Channel uncertainty in ultra-wideband communication systems." IEEE TIT 2007; arXiv:cs/0509068 (abstract/secondary).
- Luo, C., Médard, M., Zheng, L. "On approaching wideband capacity using multitone FSK." IEEE JSAC 23(9), 2005, doi:10.1109/JSAC.2005.853803 (abstract).
- Polyanskiy, Y., Poor, H. V., Verdú, S. "Minimum energy to send k bits through the Gaussian channel with and without feedback." IEEE TIT 57(8), 2011 (secondary; expansion used).
- Yang, W., Durisi, G., Polyanskiy, Y. "Minimum energy to send k bits over multiple-antenna fading channels." IEEE TIT 62(12), 2016 (Table I values; full in the GEMM review).
- Östman, J., Lancho, A., Durisi, G. et al. "Short packets over block-memoryless fading channels: pilot-assisted or noncoherent transmission?" IEEE TCOM 2019 (full in the GEMM review).
- Gross, E., Vitells, O. "Trial factors for the look elsewhere effect in high energy physics." Eur. Phys. J. C 70, 2010 (used by the theory review for K_eff).

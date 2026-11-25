# Noise environment and link budgets

Research date: 24 November 2026. This chapter turns transmit power and path
loss into received C/N0 using realistic HF noise, then gives ideal and
channel-aware airtimes. Calculations come from the programs in
[`_appraisal/algorithms-matrixModem/calc`](../../../_appraisal/algorithms-matrixModem/calc/README.md)
(`link_budget.cpp`, `mfsk_capacity.cpp`, `airtime_table.py`). The noise review
behind them is
[F50](../../highFidelity-triple_Robust_Modem/Fable-5.2/50-noise-and-interference-mf-hf.md),
which audits ITU-R P.372 editions, measured departures and impulsive statistics
in depth.

## Findings

1. **HF external noise is 20–45 dB above thermal.** With the ITU-R P.372 median
   man-made and galactic curves, a lossless short monopole sees about
   Fa = 30 dB (N0 ≈ −144 dBm/Hz) at 10 MHz in a quiet-rural environment and
   about 39 dB (−134 dBm/Hz) in a rural one; at 7 MHz the corresponding values
   are about −140 and −130 dBm/Hz. Atmospheric noise, not included in these
   curves, can dominate below ~10 MHz at night and in summer.
2. **The Sol-6.2 link arithmetic is 25–35 dB optimistic for HF.** Its
   illustrative −174/−164/−154 dBm/Hz noise densities correspond to
   Fa = 0/10/20 dB. The repository's [noise review](../../highFidelity-triple_Robust_Modem/Fable-5.2/50-noise-and-interference-mf-hf.md)
   already notes that the modem simulator default of −164 dBm/Hz is below the
   P.372 reference outputs. For 2 mW and 150 dB loss, realistic C/N0 is about
   −7 dB-Hz at a quiet-rural 7–10 MHz site, not +17 dB-Hz. At 180 dB it is
   about −37 dB-Hz.
3. **Receive antenna efficiency rarely matters; transmit antenna efficiency
   always does.** When external noise dominates the receiver's own noise, an
   inefficient receive antenna attenuates signal and noise equally. A compact or
   indoor receive antenna therefore costs little C/N0 until antenna noise falls
   near the receiver noise floor, while transmit efficiency and EIRP enter the
   budget directly.
4. **Ideal airtimes are long; channel-aware airtimes can be far longer.** At
   −10 dB-Hz the Shannon minimum for 1 KiB is about 16 hours. A noncoherent tone
   modem with an ideal outer code needs about 20 hours in a stable channel,
   about 3.4 days with 0.1 Hz Doppler spread, about 24 days at 1 Hz, and about
   eight months at 10 Hz. The gap grows as C/N0 falls because of the
   noncoherent square-law regime explained in the
   [limits chapter](03-information-limits-and-noncoherent-penalties.md).
5. **Raising transmit power can lower total transmit energy.** In disturbed
   channels below the noncoherent knee, required energy per bit falls steeply as
   C/N0 rises: at 10 Hz spread, Eb/N0 drops from about 24 dB at −10 dB-Hz to
   about 6 dB at +10 dB-Hz. In the square-law regime the rate grows with the
   square of power, so ten times more power means about a hundred times less
   airtime and about ten times less total energy. A fixed "minimum power"
   choice is not energy-optimal when the channel is not quiet.

## Reference plane and definitions

C/N0 (dB-Hz) is the received carrier power divided by the noise power spectral
density at a common reference plane. In this chapter:

- **Path loss L** is the total signal-only loss from transmitter output to the
  receiver reference plane, including antenna gains and efficiencies. It is the
  user's "path loss" parameter.
- **Fa** is the ITU-R P.372 external noise figure referred to a short lossless
  vertical monopole over perfect ground. N0 = Fa − 174 dBm/Hz.
- **Atmospheric noise** is omitted from the tabulated curves because it is
  strongly location-, season- and time-specific. [F50] summarizes P.372 values,
  e.g. summer-night atmospheric noise of about Fa 43–53 dB near 10 MHz, which
  would exceed the rural man-made curve.

When external noise dominates, the receive antenna's efficiency scales both
signal and noise, leaving C/N0 unchanged. This is why the noise is referred to
a lossless reference antenna and why "path loss" should be read as signal loss
relative to that reference.

## P.372 medians at amateur HF frequencies

Man-made noise Fam = c − d·log10(f/MHz) with (c, d) = city (76.8, 27.7),
residential (72.5, 27.7), rural (67.2, 27.7), quiet rural (53.6, 28.6); galactic
noise 52.0 − 23.0·log10(f) when it reaches the ground (above the ionospheric
critical frequency). Values in dB above kT0 (`link_budget.cpp`):

| f (MHz) | City | Residential | Rural | Quiet rural | Galactic | Quiet rural + galactic: Fa / N0 (dBm/Hz) | Rural + galactic N0 (dBm/Hz) |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1.9 | 69.1 | 64.8 | 59.5 | 45.6 | 45.6 | 48.6 / −125.4 | −114.3 |
| 3.6 | 61.4 | 57.1 | 51.8 | 37.7 | 39.2 | 41.5 / −132.5 | −122.0 |
| 5.3 | 56.7 | 52.4 | 47.1 | 32.9 | 35.3 | 37.3 / −136.7 | −126.6 |
| 7.1 | 53.2 | 48.9 | 43.6 | 29.3 | 32.4 | 34.1 / −139.9 | −130.1 |
| 10.1 | 49.0 | 44.7 | 39.4 | 24.9 | 28.9 | 30.3 / −143.7 | −134.2 |
| 14.1 | 45.0 | 40.7 | 35.4 | 20.7 | 25.6 | 26.8 / −147.2 | −138.2 |
| 18.1 | 42.0 | 37.7 | 32.4 | 17.6 | 23.1 | 24.2 / −149.8 | −141.2 |
| 21.1 | 40.1 | 35.8 | 30.5 | 15.7 | 21.5 | 22.6 / −151.4 | −143.0 |
| 28.1 | 36.7 | 32.4 | 27.1 | 12.2 | 18.7 | 19.6 / −154.4 | −146.3 |

P.372 gives decile deviations of roughly +11/−5 dB in time and ±6–8 dB in
location for the man-made categories [F50]. Measured campaigns depart from the
curves in both directions: Dutch amateur-site measurements put dense
residential areas 7–13 dB above the residential curve below 4 MHz and 1–9 dB
above it at 7–21 MHz, while German regulator measurements lie between 3 dB
below and 7 dB above at 5 and 12 MHz, and quiet sites lie near the
quiet-rural/galactic lines above 7 MHz [F50]. A 2024 Antarctic campaign (NTIA
TR-25-579) classified one remote site as quiet rural and in-town locations as
rural to residential. Treat the tabulated values as medians with ±10 dB
plausible site variation, not as a forecast for a given station.

### Impulsive noise and interference

HF noise in a few-kHz bandwidth is often impulsive. P.372-style
amplitude-probability distributions show voltage-deviation ratios Vd of about
4–11 dB in 0.25–3 kHz bandwidths, with medians 8–20 dB below rms [F50]. EbNaut
operators report that blanking 25–35% of samples around sferics gained about
20 dB at VLF (Nicholson, via the [empirical anchors](04-empirical-weak-signal-anchors.md)).
At HF the practical floor is often other signals: in a Belgian example, 3 kHz
channels at 2–9 MHz were unavailable about a third of the time on average
[F50]. Three receiver consequences follow:

- Front-end clipping or blanking before 4-bit quantization is part of the
  detector, not cosmetic. Low-bit quantization efficiencies assume
  noise-dominated Gaussian-like samples (see the
  [precision chapter](07-matrix-hardware-and-precision.md)).
- Null statistics must be calibrated on noise from the same front end, not
  derived from Gaussian theory, because huge hypothesis banks turn rare
  impulses into false candidates.
- Absorption events can lower noise along with the signal: during an X1.6
  fadeout on a 7 MHz NVIS link the signal fell more than 40 dB but the noise
  fell 12 dB (Witvliet 2022; [channel chapter](01-hf-channel-moderate-to-worst.md)).

## C/N0 and ideal airtime

Three noise densities bracket mid-HF medians: −150 dBm/Hz (quiet rural or
galactic, upper HF), −140 dBm/Hz (quiet rural near 7–10 MHz, rural near
20 MHz) and −130 dBm/Hz (rural near 7 MHz, residential near 14 MHz).

| N0 (dBm/Hz) | Pt | L (dB) | C/N0 (dB-Hz) | 128 b, Shannon | 128 b, finite-k orthogonal (ε = 10⁻³) | 1 KiB, Shannon |
|---:|---:|---:|---:|---:|---:|---:|
| −150 | 2 mW | 150 | +3.0 | 44 s | 66 s | 47 min |
| −150 | 2 mW | 170 | −17.0 | 74 min | 109 min | 3.3 d |
| −150 | 100 mW | 150 | +20.0 | 0.9 s | 1.3 s | 57 s |
| −150 | 100 mW | 180 | −10.0 | 15 min | 22 min | 16 h |
| −140 | 2 mW | 150 | −7.0 | 7.4 min | 11 min | 7.9 h |
| −140 | 2 mW | 160 | −17.0 | 74 min | 109 min | 3.3 d |
| −140 | 2 mW | 180 | −37.0 | 5.1 d | 7.6 d | 329 d |
| −140 | 10 mW | 150 | 0.0 | 89 s | 2.2 min | 95 min |
| −140 | 100 mW | 150 | +10.0 | 8.9 s | 13 s | 9.5 min |
| −140 | 100 mW | 170 | −10.0 | 15 min | 22 min | 16 h |
| −140 | 100 mW | 180 | −20.0 | 2.5 h | 3.7 h | 6.6 d |
| −130 | 2 mW | 150 | −17.0 | 74 min | 109 min | 3.3 d |
| −130 | 100 mW | 150 | 0.0 | 89 s | 2.2 min | 95 min |
| −130 | 100 mW | 180 | −30.0 | 25 h | 37 h | 66 d |

The full grid (2/10/100 mW × 140–180 dB × three noise levels) is in
`calc/results/link_budget.md`. "Shannon" is the wideband energy bound
k·ln2/(C/N0). The finite-k column uses the exact error probability of coherent
orthogonal signaling with M = 2^128 waveforms at word error 10⁻³
(Eb/N0 = 0.11 dB), an achievable reference that is slightly tighter than the
truncated Polyanskiy–Poor–Verdú expansion (0.31 dB).

## Channel-aware airtime

The next table uses the best-symbol-duration noncoherent MFSK rate from the
[limits chapter](03-information-limits-and-noncoherent-penalties.md):
symbol-wise mutual information with an ideal outer code and ideal interleaving,
2.2 kHz band, Rayleigh fading with Gaussian Doppler spectra (2σ convention), no
synchronization overhead, no finite-length penalty and no outage. It is still
optimistic, but it charges the noncoherent and Doppler penalties that the
Shannon column ignores. Generated by `airtime_table.py`.

| C/N0 (dB-Hz) | Shannon 1 KiB | Stable phase (AWGN, unknown phase), 128 b / 1 KiB | 0.1 Hz spread | 1 Hz spread | 10 Hz spread |
|---:|---:|---:|---:|---:|---:|
| +20 | 57 s | 1.6 s / 100 s | 2.2 s / 2.4 min | 2.2 s / 2.4 min | 2.5 s / 2.7 min |
| +10 | 9.5 min | 13 s / 14 min | 18 s / 19 min | 20 s / 21 min | 47 s / 51 min |
| +3 | 47 min | 62 s / 66 min | 89 s / 95 min | 2.6 min / 2.7 h | 15 min / 15 h |
| 0 | 95 min | 2.0 min / 2.1 h | 3.1 min / 3.3 h | 7.6 min / 8.1 h | 55 min / 2.5 d |
| −7 | 7.9 h | 9.7 min / 10 h | 25 min / 27 h | 2.4 h / 6.4 d | 22 h / 60 d |
| −10 | 16 h | 19 min / 20 h | 76 min / 3.4 d | 9.1 h / 24 d | 3.7 d / 235 d |
| −17 | 3.3 d | (1.6 h / 4.2 d)\* | 25 h / 66 d | 9.2 d / 1.6 y | 92 d / 16 y |
| −20 | 6.6 d | (3.2 h / 8.5 d)\* | 3.9 d / 250 d | 37 d / 6.4 y | 1.0 y / 64 y |

\* Constant-Eb/N0 extrapolation requiring symbols much longer than 300 s,
which needs extreme frequency stability and is not realistic on HF skywave.
The 0.1 Hz column below −10 dB-Hz is pessimistic because the calculation caps
the symbol at 60 s.

Reading the table with the user's important case (2–100 mW, >150 dB, HF):

- **2 mW at 150 dB, quiet-rural 7–10 MHz (C/N0 ≈ −7 dB-Hz).** A 128-bit message
  is feasible in about 10–25 minutes on a quiet path, about 2.4 hours at 1 Hz
  spread, and nearly a day at 10 Hz. A 1 KiB file needs about a day on a quiet
  path, roughly a week in moderate conditions, and is impractical in severe
  conditions.
- **100 mW at 170 dB (C/N0 ≈ −10 dB-Hz).** Similar to the previous row's order:
  hours for short messages in quiet and moderate conditions, days for files.
- **Anything below about −17 dB-Hz** is a multi-day proposition for short
  messages even on quiet paths and is outside what disturbed HF channels can
  deliver. Outages (MUF closure ~6 h/day on fixed frequencies, shortwave
  fadeouts, PCA, storms) then dominate the completion probability, because the
  message spans many of them.

These durations are information-theoretic expectations, not demonstrated
performance. A practical design adds synchronization, finite-length coding,
reliability margin (50% → 99% success costs about 2.2 dB for short messages)
and outage margin. Existing weak-signal modes sit 4–7 dB above the Shannon
limit on AWGN ([anchors](04-empirical-weak-signal-anchors.md)).

## Energy-optimal power under fading

Below the noncoherent knee the information rate falls with the square of C/N0,
so the energy per bit rises as C/N0 falls. Energy per bit is minimized near or
above the knee. From the capacity results (best symbol duration):

| Doppler spread (2σ) | Eb/N0 at −10 dB-Hz | at 0 dB-Hz | at +10 dB-Hz | Minimum over C/N0 (approx.) |
| --- | ---: | ---: | ---: | --- |
| none (stable phase) | −0.5 dB | −0.3 dB | +0.1 dB | ≈ −0.5 dB at low C/N0 |
| 0.1 Hz | 5.5 dB | 1.6 dB | 1.5 dB | ≈ 1.3–1.4 dB near +4 to +8 dB-Hz |
| 1 Hz | 14.1 dB | 5.5 dB | 1.9 dB | ≈ 1.9 dB near +10 to +14 dB-Hz |
| 10 Hz | 24.0 dB | 14.1 dB | 5.7 dB | ≈ 2.9 dB near +20 dB-Hz |
| 30 Hz | 28.8 dB | 18.8 dB | 9.5 dB | ≈ 3.9 dB at +20 dB-Hz and above |

For the configuration problem this means:

- "Transmit Power" and "Target SNR dB/1Hz" should be chosen together with the
  expected Doppler category. In quiet conditions low power costs little energy
  efficiency; in disturbed conditions it costs a great deal.
- A transmitter limited by average energy rather than peak power could use
  higher power for less time (flash-like signaling). Whether "transmit power"
  in the operating configuration is a peak or average limit is a requirement
  question that changes the optimal design. The LPI cost of higher peak power
  is analysed in the [observer chapter](06-observer-lpi-and-interference-perception.md).

## Other paths

Sub-9 kHz, 1 and 30 MHz groundwave and Earth–Moon–Earth links have different
noise, loss and coherence. Their noise is covered in
[F12](../../highFidelity-triple_Robust_Modem/Fable-5.2/12-sub9khz-noise.md) and
[F50]; see the [other-paths chapter](10-other-propagation-paths.md) for how the
same budget method applies there.

## Sources

- ITU-R P.372-18 (09/2026), Radio noise; man-made noise constants and decile
  deviations as audited in [F50] (full text read there).
- Polyanskiy, Poor & Verdú, "Minimum energy to send k bits through the Gaussian
  channel with and without feedback," IEEE TIT 57(8), 2011,
  doi:10.1109/TIT.2011.2158476 (expansion form used; exact orthogonal values
  computed in `finite_energy.cpp`).
- Witvliet 2022, NVIS solar-flare observation (full text; see the channel
  chapter's source list).
- Prior Matrix arithmetic: [Sol-6.2 information limits](../Sol-6.2/information-limits.md)
  (superseded noise assumptions; formulas remain valid).

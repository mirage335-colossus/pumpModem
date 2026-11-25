
```
There is a hard limit in disturbed channels. What matters is how much signal energy arrives within one fading coherence interval. For a power-limited transmitter, a known formula caps the achievable rate, and my independent simulation of long-symbol tone signaling reaches 97–99.8% of it. At −10 dB-Hz the minimum energy per bit is about:
Doppler spread	Eb/N0 needed
0.1 Hz	5.4 dB
1 Hz	14 dB
10 Hz	24 dB
30 Hz	29 dB

No waveform or extra computation beats this, assuming Rayleigh fading and a smooth Doppler spectrum.
```



No study I found shows anything substantially better than MFSK on Doppler-spread channels, in sensitivity or in receiver computation. The quoted limit is correct and, within its model, close to a worst case. Most of the large losses seen in practice come from the receiver, not from MFSK itself.

**The quoted limit.** I reproduced it independently: 5.39 / 14.09 / 23.92 / 28.68 dB at −10 dB-Hz. It assumes a transmitter limited in peak power (constant envelope), Rayleigh fading, one receive antenna, known timing and Gaussian noise.
- **What sets it.** The governing quantity is the energy received per effective coherence interval. Once that is small, each dB of lost C/N₀ costs about 1 dB more energy per bit, and halving transmit power quadruples the time.
- **The table is for one C/N₀ only.** At −20 dB-Hz with 1 Hz of spread, the bound needs about 29 days per 100 bits. The weak half of the 2 mW–100 mW / 150–180 dB design space can't carry a short message over a fading skywave path. Neither waveform choice nor computation changes that; only more peak power, receive aperture, lower noise, or a narrower or partly specular path does.
- **It is close to a worst case.** For a given Doppler autocorrelation, Rayleigh is the worst fading law. Koch–Lapidoth proved only the high-SNR version; the low-SNR version is my derivation from the same step. For a given RMS spread, the Gaussian spectrum is within 0.2 dB of the worst possible shape.

**Where the bound is too pessimistic.** Any specular power changes it enormously: about 9% specular power takes the 30 Hz figure from 28.7 dB to 8.8 dB. Quiet one-hop HF paths often measure as narrow, slowly drifting lines rather than a Gaussian spread. Extra receive antennas, especially polarization diversity, help, as does a transmitter allowed to burst above its average power.

**Where it is too optimistic.** Finite messages, nonstationary paths and outages, unknown timing (about 1 dB for 2¹⁸ candidate start times), and two similar rays that fade worse than Rayleigh all make reality harder.

**Practical receivers versus the bound:**
- **Adapted long-dwell tones reach the bound.** Tones with a long dwell and a receiver weighted to the actual Doppler spread come within 0.2 dB of it in every spread category. That receiver is the asymptotic estimator-correlator; Q65 already uses it in the field.
- **Finite band.** In a real 2.2 kHz band, a single configuration (100 Hz tone spacing, 22 tones) stays within about 1.1 dB of time across 0.1–30 Hz spreads at −10 dB-Hz.
- **Fixed short symbols are the big loss.** FT8-style 160 ms single-bin detection needs 14 dB more time than the bound on a 0.1 Hz path at −10 dB-Hz.
- **A fixed spread assumption is fragile.** Designing for the wrong spread costs up to 20 dB, and picking the spread separately in each symbol costs 3–11 dB at low SNR. The receiver must adapt over the whole message.
- **Chirps** are 1.3–1.5 dB worse than tones in two-path Rayleigh fading at low SNR, because they separate the paths and pay to learn each one.
- **PSK** can reach the bound in theory, but only with long-memory joint detection. Plain differential PSK gets 23% of the bound's rate.
- **Computation.** An FFT tests every tone hypothesis at once, so MFSK is already near minimal per hypothesis. The cost that dominates everywhere is the number of timing, frequency, drift and spread hypotheses.

**Real theoretical weaknesses of MFSK, and their fixes:**
- **Impulsive noise** can collapse an energy detector; clip or blank before the transform.
- **Specular paths** need a long-coherence, drift-tracking receiver branch.
- **Narrow tone spacing** fails in fast fading when dwells are long.
- **Short dwells** chosen for a stronger signal than actually arrives cost sensitivity. A dwell that turns out too long only caps throughput, so configure for the weakest signal the purpose must survive.

**Not established.** The calculations assume known timing and ideal interleaving. Acquisition, coding, physical completion and a native receiver are untested. Measured HF spectra finer than 0.1 Hz, and how much of the power is specular, are scarce in what I could access. A better design may still exist.

I didn't deploy RunPod. Everything ran locally; the longest job, the finite-alphabet Monte Carlo, took 21 minutes. A citation check caught 11 DOIs I had built from patterns; all are now corrected, and one unfindable link was removed. Nothing is committed. The natural next experiment is the adaptive receiver in the native stack, with acquisition and impulsive noise included.

Files are in `_ref/algorithms-matrixModem/Sol-6.2/` (start with the first; README, research index, waveform guide and brief now link to it):
- [fading-limit-and-mfsk-alternatives.md](_ref/algorithms-matrixModem/Sol-6.2/fading-limit-and-mfsk-alternatives.md)
- [mfsk-alternatives-practice-and-computation.md](_ref/algorithms-matrixModem/Sol-6.2/mfsk-alternatives-practice-and-computation.md)
- [hf-channel-structure-and-extremes.md](_ref/algorithms-matrixModem/Sol-6.2/hf-channel-structure-and-extremes.md)
- [coherence-limits/README.md](_appraisal/algorithms-matrixModem/coherence-limits/README.md) — the calculation programs and results

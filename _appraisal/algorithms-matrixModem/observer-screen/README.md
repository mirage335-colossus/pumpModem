# Idealized wideband observer screen


This small calculation isolates the energy detector's search penalty as a function of frequency uncertainty and observation duration. It is an observer-model illustration, not a Matrix Modem result, an LPI guarantee, or a propagation simulation.

## Model

The signal is a single constant-amplitude tone in each 160 ms look. Tone locations are orthogonal on a 6.25 Hz grid. `K` counts possible tone locations, increasing the observer's frequency search span from one grid location to 352 locations (about 2.2 kHz). `L` is the number of looks, so total observation duration is `0.16 L` seconds. One known FHSS schedule selects one tone per look. Each candidate schedule predicts one tone at each look. The schedules are constructed to be mutually orthogonal. Under the null hypothesis every complex coefficient is independent circular Gaussian noise with variance `N0`. Under the alternative, the signal adds deterministic complex coefficients, with unknown phase, whose total energy is `E`. The signal energy divided by `N0` is held fixed as `K` and `L` change.

The primary statistic for each searched schedule is its summed energy over `L` looks. A single known schedule has a central chi-square null distribution with `2L` real degrees of freedom and a noncentral chi-square alternative with noncentrality `2E/N0`. A bank of `J` mutually orthogonal schedules reports the maximum score. Its family-wise false-alarm threshold is set to `Pfa=10^-3`, and the required `E/N0` is solved for `Pd=0.9`. The full search uses `J=K`. The sparse observer searches `J=min(K,16)` schedules, with the true schedule included in that bank. The wideband radiometer sums all `K*L` tone-look energies and uses the same total energy, false alarm rate, and detection probability.

The chi-square CDF is evaluated as a Poisson mixture of regularized gamma CDFs. This supplies an exact numerical reference under the independent complex-Gaussian, fixed-signal model, subject to floating-point evaluation and a negligible truncated Poisson tail. The experiment does not model the receiver's coded alphabet, ciphertext, an observer's search algorithm, or possible information in non-energy waveform features.

## Selected result

At `K=352`, `L=1`, the whole-band radiometer requires `E/N0=19.55 dB`. A known schedule requires `10.76 dB`, a 16-schedule search requires `11.92 dB`, and a search across all 352 schedules requires `12.90 dB`. With `L=16` looks (2.56 seconds) at the same *total* signal energy, the corresponding values are `25.27`, `14.05`, `14.92`, and `15.66 dB`.

The single-look `L=1`, `K=1` row gives `10.7586 dB` for fixed amplitude and unknown phase under energy detection. A separate exact Rayleigh-fading control uses one complex coefficient whose power is exponential. For `Pfa=0.001` and `Pd=0.9`, it requires mean `E/N0=18.0998 dB`. The control evaluates `Pfa=exp(-threshold)` and `Pd=exp(-threshold/(1+rho))`, with `threshold=-ln(Pfa)`, and returns the requested probabilities. This large difference illustrates how a deep fade in even one observation changes sensitivity. It does not represent coding or interleaving across fades.

The companion [`detectability-cn0.svg`](results/detectability-cn0.svg) fixes `K=352` and converts each energy threshold to mean received `C/N0` in dB-Hz as `10log10(E/N0) - 10log10(T)`, where `T=0.16 L` seconds. At `T=0.16 s`, the radiometer, known schedule, sparse 16-schedule search and full search require respectively `27.51`, `18.72`, `19.88` and `20.86 dB-Hz`. At `T=2.56 s`, they require `21.19`, `9.96`, `10.84` and `11.58 dB-Hz`. Thus the minimum mean power per noise density decreases with a longer observation even as the required total energy increases. This is the fixed-total-energy comparison. At fixed received power, total energy instead grows in proportion to duration, so the plot should not be read as an energy efficiency gain.

For fixed total energy and a known true schedule, adding frequency alternatives does not cost the matched detector energy in this orthogonal idealization. Searching more alternatives adds a multiple-comparison cost, while summing noise across the full band makes the radiometer's required energy grow more rapidly. These curves describe detector structure under the stated model. They do not imply that a real adversary uses only a radiometer, that FHSS conceals spectral or cyclostationary structure, or that encryption makes signal presence unknown.

## Limits and next discriminator

- The 352-location grid spans approximately 2.2 kHz at 6.25 Hz spacing. The code assumes perfect tone orthogonality and a correct per-look schedule. Offset error, oscillator drift, Doppler spread, multipath, filter shape, spectral leakage, impulsive noise, narrowband interference, correlated fading, and noise-power uncertainty are absent.
- The main curves use fixed amplitude, not Rayleigh fading. The separate single-look Rayleigh control is an analytic boundary case, not a comparison sweep. A realistic follow-up should use independent, correlated, and measured fading together with a fixed total energy and calibrated receiver and observer noise models.
- Holding total energy fixed while increasing `L` lowers average received power. At fixed power, energy instead increases with observation time. Both conventions matter for a covert-communication claim.
- Search schedules are assumed mutually orthogonal. Real schedule banks can overlap strongly, and unknown schedule generation may create further statistics beyond these energy scores. The sparse curve conditions on the correct schedule being in the 16-entry bank.
- The wideband radiometer uses known noise variance. Noise uncertainty, an adversary's antenna and front end, propagation to the adversary, observation bandwidth, operational threshold, and false-alarm budget are unspecified.
- This model reports detection energy at fixed error probabilities. It does not calculate information leakage, decoding success, semantic inference, legal detectability, or low-perception-of-interference performance.

The next useful calculation is a paired observer and intended-receiver study using the same finite waveform records, with fixed-energy and fixed-power panels, uncertain noise, realistic schedule overlap, and channel trajectories from the maintained HF model evidence. Keep the observer's signal prior explicit, and measure false alarms and misses on independent holdouts.

## Reproduce

```sh
c++ -O3 -std=c++20 -Wall -Wextra -Wpedantic detectability.cpp -o /tmp/matrix_observer_screen
/tmp/matrix_observer_screen > results/detectability.csv
python3 -I plot.py
```

`results/detectability.csv` contains all 30 combinations of six `K` values and five `L` values. `results/detectability.svg` plots the required total energy versus `K`; color distinguishes detector and line style distinguishes observation duration. `results/detectability-cn0.svg` plots the converted required received `C/N0` versus duration for `K=352`. Both searched banks condition on the correct schedule being present. The executable prints the independent Rayleigh control to standard error. An independent integer-Poisson recurrence check of all 120 rounded energy thresholds recovered `Pd=0.9` within `7.7×10⁻⁸`. The CSV and the line plots are retained as reproducible evidence for this appraisal. Replace them when a matched sampled waveform and receiver/observer capture study supersedes this illustration.

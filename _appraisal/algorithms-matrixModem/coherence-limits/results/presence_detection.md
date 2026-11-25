# Presence detection of a known keyed pattern versus a 100-bit message

P_FA = 1e-06, P_D = 0.9 (z = 4.753 + 1.282), Gaussian Doppler spectra, known timing and frequency, optimum quadratic detector, normal approximation.

`x` is the received energy per effective coherence interval, (C/N0)/B_eff; `looks` is T*B_eff, the number of effectively independent coherence intervals observed.

| C/N0 | 2σ | x | detect pattern | looks | 100 bits at bound | ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| -20 dB-Hz | no fading | — | 30.4 min | — | 116 min | 3.8 |
| -20 dB-Hz | 0.1 Hz | -12.5 dB | 18.4 h | 1.18e+04 | 2.97 d | 3.9 |
| -20 dB-Hz | 1 Hz | -22.5 dB | 7.49 d | 1.15e+06 | 28.6 d | 3.8 |
| -20 dB-Hz | 10 Hz | -32.5 dB | 74.7 d | 1.14e+08 | 285 d | 3.8 |
| -20 dB-Hz | 30 Hz | -37.3 dB | 224 d | 1.03e+09 | 2.34 yr | 3.8 |
| -10 dB-Hz | no fading | — | 3.04 min | — | 11.6 min | 3.8 |
| -10 dB-Hz | 0.1 Hz | -2.5 dB | 14 min | 149 | 57.7 min | 4.1 |
| -10 dB-Hz | 1 Hz | -12.5 dB | 111 min | 1.18e+04 | 7.12 h | 3.9 |
| -10 dB-Hz | 10 Hz | -22.5 dB | 18 h | 1.15e+06 | 2.86 d | 3.8 |
| -10 dB-Hz | 30 Hz | -27.3 dB | 2.24 d | 1.03e+07 | 8.54 d | 3.8 |
| 0 dB-Hz | no fading | — | 18.2 s | — | 69.3 s | 3.8 |
| 0 dB-Hz | 0.1 Hz | 7.5 dB | 34.8 s † | 6.17 | 110 s | 3.1 |
| 0 dB-Hz | 1 Hz | -2.5 dB | 83.8 s | 149 | 5.77 min | 4.1 |
| 0 dB-Hz | 10 Hz | -12.5 dB | 11.1 min | 1.18e+04 | 42.7 min | 3.9 |
| 0 dB-Hz | 30 Hz | -17.3 dB | 32.6 min | 1.04e+05 | 2.08 h | 3.8 |

† fewer than 30 effective looks: the normal approximation, and the ergodic treatment of fading, are unreliable; an outage analysis applies instead.

Quadratic-regime check: for x below about −10 dB the detection time is close to `36.4·B_eff/(C/N0)²`, so halving power quadruples it, exactly as for message time at the bound.

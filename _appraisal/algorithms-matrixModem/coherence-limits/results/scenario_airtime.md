# Plausible transmit time for 100 bits over HF skywave

Wall-clock transmit time = time at the bound × 2.51 (receiver allowance 4 dB of time) / 0.67 (path availability). Gaussian Doppler spectra (`2σ`), Rayleigh fading, constant envelope, one receive branch, stationary bound at the stated C/N0.

| scenario | C/N0 | no fading | 0.05 Hz | 0.1 Hz | 1 Hz | 10 Hz |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 100 mW, 150 dB, quiet rural −144 | +14 dB-Hz | 10 s | 11 s | 11 s | 13 s | 29 s |
| 100 mW, 150 dB, rural −130 | +0 dB-Hz | 4.4 min | 5.8 min | 6.9 min | 22 min | 2.7 h |
| 2 mW, 150 dB, quiet rural −144 | -3 dB-Hz | 8.7 min | 14 min | 17 min | 74 min | 10 h |
| 100 mW, 170 dB, −140 | -10 dB-Hz | 44 min | 2.3 h | 3.6 h | 27 h | 11 d |
| 2 mW, 150 dB, rural −130 | -17 dB-Hz | 3.6 h | 38 h | 2.9 d | 27 d | 8.8 mo |
| 100 mW, 180 dB, −140 | -20 dB-Hz | 7.3 h | 5.8 d | 11 d | 3.5 mo | 2.9 yr |
| 2 mW, 165 dB, −140 | -22 dB-Hz | 11 h | 14 d | 28 d | 8.9 mo | 7.4 yr |
| 100 mW, 180 dB, rural −130 | -30 dB-Hz | 3.0 d | 18 mo | 2.9 yr | 29 yr | 293 yr |
| 2 mW, 180 dB, −140 | -37 dB-Hz | 15 d | 37 yr | 74 yr | 737 yr | 7369 yr |
| 2 mW, 180 dB, rural −130 | -47 dB-Hz | 5.0 mo | 3685 yr | 7370 yr | 73692 yr | 736916 yr |

Time at the bound alone (no receiver or availability allowance):

| scenario | C/N0 | no fading | 0.05 Hz | 0.1 Hz | 1 Hz | 10 Hz |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 100 mW, 150 dB, quiet rural −144 | +14 dB-Hz | 3 s | 3 s | 3 s | 4 s | 8 s |
| 100 mW, 150 dB, rural −130 | +0 dB-Hz | 69 s | 93 s | 110 s | 5.8 min | 43 min |
| 2 mW, 150 dB, quiet rural −144 | -3 dB-Hz | 2.3 min | 3.6 min | 4.6 min | 20 min | 2.8 h |
| 100 mW, 170 dB, −140 | -10 dB-Hz | 12 min | 37 min | 58 min | 7.1 h | 2.9 d |
| 2 mW, 150 dB, rural −130 | -17 dB-Hz | 58 min | 10 h | 19 h | 7.2 d | 2.3 mo |
| 100 mW, 180 dB, −140 | -20 dB-Hz | 116 min | 37 h | 3.0 d | 29 d | 9.3 mo |
| 2 mW, 165 dB, −140 | -22 dB-Hz | 3.1 h | 3.8 d | 7.3 d | 2.4 mo | 23 mo |
| 100 mW, 180 dB, rural −130 | -30 dB-Hz | 19 h | 4.7 mo | 9.4 mo | 7.8 yr | 78 yr |
| 2 mW, 180 dB, −140 | -37 dB-Hz | 4.0 d | 9.8 yr | 20 yr | 196 yr | 1956 yr |
| 2 mW, 180 dB, rural −130 | -47 dB-Hz | 40 d | 978 yr | 1956 yr | 19558 yr | 195581 yr |

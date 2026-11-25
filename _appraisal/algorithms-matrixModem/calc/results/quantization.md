## (a) Weak-signal correlation efficiency, Gaussian input, optimal scale

| quantizer | levels | eta | loss (dB) | best step / sigma |
|---|---:|---:|---:|---:|
| 1-bit sign | 2 | 0.6366 | 1.961 | 1.182 |
| 2-bit midrise {+-1,+-3}/2 | 4 | 0.8812 | 0.549 | 0.998 |
| 3-bit midrise (8 levels) | 8 | 0.9626 | 0.166 | 0.583 |
| INT3 two's complement -4..3 | 8 | 0.9594 | 0.180 | 0.601 |
| 4-bit midrise (16 levels) | 16 | 0.9885 | 0.050 | 0.334 |
| INT4 two's complement -8..7 | 16 | 0.9881 | 0.052 | 0.338 |
| INT4 symmetric -7..7 | 15 | 0.9871 | 0.056 | 0.355 |
| FP4 E2M1 (15 values) | 15 | 0.9873 | 0.055 | 0.488 |
| E2M1 integer subset {0,+-1,+-2,+-3,+-4,+-6} | 11 | 0.9801 | 0.087 | 0.404 |
| INT4/E2M1 common integers -4..4 | 9 | 0.9693 | 0.135 | 0.533 |
| INT8 symmetric -127..127 | 255 | 0.9999 | 0.000 | 0.031 |

## (b) Tone-template quantization efficiency (I and Q quantized separately)

| codebook | amplitude (codebook units) | efficiency | loss (dB) |
|---|---:|---:|---:|
| INT4 symmetric -7..7 | 3.5 | 0.9856 | 0.063 |
| INT4 symmetric -7..7 | 5.0 | 0.9943 | 0.025 |
| INT4 symmetric -7..7 | 6.0 | 0.9960 | 0.018 |
| INT4 symmetric -7..7 | 7.0 | 0.9970 | 0.013 |
| INT4 symmetric -7..7 | 7.5 | 0.9969 | 0.014 |
| INT4 symmetric -7..7 | 8.0 | 0.9950 | 0.022 |
| FP4 E2M1 | 4.0 | 0.9938 | 0.027 |
| FP4 E2M1 | 5.0 | 0.9883 | 0.051 |
| FP4 E2M1 | 6.0 | 0.9918 | 0.036 |
| FP4 E2M1 | 7.0 | 0.9907 | 0.041 |
| common integers -4..4 | 3.0 | 0.9853 | 0.064 |
| common integers -4..4 | 4.0 | 0.9913 | 0.038 |
| common integers -4..4 | 4.5 | 0.9913 | 0.038 |
| ternary {-1,0,1} | 1.0 | 0.9120 | 0.400 |
| ternary {-1,0,1} | 1.5 | 0.9195 | 0.364 |
| binary {-1,1} (sign of I,Q) | 1.0 | 0.8097 | 0.917 |

## (c) Monte Carlo: weak tone, N=512 samples, 4-bit data x 4-bit template, deflection of |z|^2

Per-sample SNR -20 dB, coherent gain 27.1 dB -> ideal Es/N0 = 7.09 dB.
Deflection (mean shift / null std of |z|^2): unquantized 5.0921, 4-bit data & template 5.0244 -> ratio 0.9867 (0.058 dB in SNR terms); predicted from (a)x(b): 0.9852

## (d) Symbol-wise MI of noncoherent M-FSK (rank-1 Rayleigh) with 16-level quantized cell energies

Energies normalized to the noise mean (e ~ Exp(1) noise-only). Quantizers: 'uniform' step d on [0,16d); 'equiprob' thresholds at noise-only quantiles; 'log' thresholds geometric. Receiver uses exact bin likelihoods.

| M | Es/N0 (dB) | MI unquantized (bit) | uniform best | equiprob | log-spaced | MI ratio unquantized/uniform (dB) |
|---:|---:|---:|---:|---:|---:|---:|
| 16 | 0 | 0.3727 | 0.3679 | 0.3017 | 0.3644 | 0.055 |
| 16 | 3 | 0.8996 | 0.8920 | 0.6966 | 0.8807 | 0.037 |
| 16 | 6 | 1.6815 | 1.6728 | 1.2669 | 1.6563 | 0.022 |
| 16 | 10 | 2.7241 | 2.7161 | 2.0545 | 2.7016 | 0.013 |
| 16 | 14 | 3.4028 | 3.3974 | 2.6064 | 3.3908 | 0.007 |
| 64 | 0 | 0.4181 | 0.4116 | 0.3222 | 0.4075 | 0.068 |
| 64 | 3 | 1.0869 | 1.0761 | 0.7584 | 1.0583 | 0.044 |
| 64 | 6 | 2.1953 | 2.1806 | 1.4085 | 2.1483 | 0.029 |
| 64 | 10 | 3.8284 | 3.8159 | 2.3445 | 3.7837 | 0.014 |
| 64 | 14 | 4.9658 | 4.9575 | 3.0285 | 4.9398 | 0.007 |
| 256 | 0 | 0.4366 | 0.4286 | 0.3284 | 0.4242 | 0.081 |
| 256 | 3 | 1.1905 | 1.1754 | 0.7761 | 1.1526 | 0.056 |
| 256 | 6 | 2.5624 | 2.5414 | 1.4478 | 2.4912 | 0.036 |
| 256 | 10 | 4.7840 | 4.7642 | 2.4217 | 4.7069 | 0.018 |
| 256 | 14 | 6.4379 | 6.4255 | 3.1359 | 6.3895 | 0.008 |

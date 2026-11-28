# Search time against block duration, sub-9 kHz configuration

Arithmetic on measured times; the rules and what is scaled are in the head of
`search_margins.py`. Block: 512 tones of 10.24 s, 5,242.88 s.

## Measured inputs

| Quantity | Value |
| --- | ---: |
| Laptop, one full-ladder decode of 2^24 messages (one hypothesis) | 2.536 s |
| Laptop, transform route per message and timing: gather / rung 0 per drift / lower rungs | 0.419 / 0.480 / 2.844 us |
| GPU, one full-ladder decode of 2^24 messages (one hypothesis) | 0.0704 s |
| GPU, FP16 matrix product route per message and timing, 2294 rows | 33.9 ns (404 nominal TFLOP/s in the product) |
| GPU, the same with 8520 rows (696 bins) | 194.5 ns |
| GPU, cuFFT route per message and timing, 174 bins | 247.2 ns |

## Measured case: 174 frequency bins, 4 drift hypotheses, 20 timing offsets, 24 bits

| Route | Search time | Block duration / search time |
| --- | ---: | ---: |
| Laptop, a full decode per hypothesis (13,920 decodes) | 35,301 s | 0.15 |
| Laptop, FFT across tone sums (FFTW, 12 threads), 20 offsets executed | 1,643 s | 3.2 |
| GPU, a full decode per hypothesis (13,920 decodes) | 980 s | 5.4 |
| GPU, cuFFT across tone sums | 83 s | 63 |
| GPU, FP16 matrix product across tone sums (Tensor Cores) | 11 s | 461 |

## Reference classes: block duration divided by search time

At or above 1 the device keeps up with the block. Only the TCXO row with 24 bits is measured;
the rest is scaled.

### 24-bit block

| Reference at each end | Hypotheses on the whole block | Laptop, per hypothesis | Laptop, transform | GPU, per hypothesis | GPU, matrix product |
| --- | ---: | ---: | ---: | ---: | ---: |
| GPS-locked, corrections logged | 1 | 2,067 | 2,067 | 74,483 | 74,483 |
| GPS-disciplined, slewed unseen at 1 ppm | 5,742 | 0.36 | 16 | 13 | 1,905 |
| TCXO, 1 ppm, 0.1 ppm/h, 0.1 s | 13,920 | 0.15 | 3.3 | 5.4 | 461 |
| Crystal, 20 ppm, 1 ppm/h, 1 s | 22,895,400 | 9e-05 | 0.012 | 0.0033 | 0.47 |
| Sound-card clock, 100 ppm, 5 ppm/h, 1 s | 565,414,400 | 3.7e-06 | 0.00057 | 0.00013 | 0.023 |

### 16-bit block

| Reference at each end | Hypotheses on the whole block | Laptop, per hypothesis | Laptop, transform | GPU, per hypothesis | GPU, matrix product |
| --- | ---: | ---: | ---: | ---: | ---: |
| GPS-locked, corrections logged | 1 | 529,250 | 529,250 | 19,067,727 | 19,067,727 |
| GPS-disciplined, slewed unseen at 1 ppm | 5,742 | 92 | 4,188 | 3,321 | 487,590 |
| TCXO, 1 ppm, 0.1 ppm/h, 0.1 s | 13,920 | 38 | 836 | 1,370 | 117,935 |
| Crystal, 20 ppm, 1 ppm/h, 1 s | 22,895,400 | 0.023 | 3.1 | 0.83 | 119 |
| Sound-card clock, 100 ppm, 5 ppm/h, 1 s | 565,414,400 | 0.00094 | 0.15 | 0.034 | 5.9 |

### 10-bit block

| Reference at each end | Hypotheses on the whole block | Laptop, per hypothesis | Laptop, transform | GPU, per hypothesis | GPU, matrix product |
| --- | ---: | ---: | ---: | ---: | ---: |
| GPS-locked, corrections logged | 1 | 33,871,982 | 33,871,982 | 1,220,334,507 | 1,220,334,507 |
| GPS-disciplined, slewed unseen at 1 ppm | 5,742 | 5,899 | 268,021 | 212,528 | 31,205,745 |
| TCXO, 1 ppm, 0.1 ppm/h, 0.1 s | 13,920 | 2,433 | 53,501 | 87,668 | 7,547,837 |
| Crystal, 20 ppm, 1 ppm/h, 1 s | 22,895,400 | 1.5 | 196 | 53 | 7,631 |
| Sound-card clock, 100 ppm, 5 ppm/h, 1 s | 565,414,400 | 0.06 | 9.3 | 2.2 | 377 |

## Not in these figures

- Forming the bin values for each timing offset (the transform stage) and the presence test.
- At rungs 1 and 2 a drifting reference moves the signal by up to 1.5 bins between windows;
  summing along tilted bin paths in the epilogue covers that and was not timed.
- Energies over 512 bins per band instead of 174 on the laptop (a small part of the transform time).
- Tone lengths other than 10.24 s: with shorter tones the gather grows with the tone count and
  the tone values would be summed into about 512 sub-blocks before the transform; not built.

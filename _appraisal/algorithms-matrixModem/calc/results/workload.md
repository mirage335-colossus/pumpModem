| Workload | Tera-ops per signal-second | % of RTX 4090 INT4 | % of RTX 4060 Ti 16GB INT4 | % of RTX 5090 FP4 | % of RTX PRO 6000 FP4 | % of RTX PRO 6000 INT8 | % of Radeon 780M IU4 (HIP) | Assumptions |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| W1 tone bank DFT-GEMM, T=0.16 s | 2.7e-05 | 2e-06 | 7.7e-06 | 1.6e-06 | 1.3e-06 | 2.7e-06 | 8.1e-05 | N=384, 352 bins, 4 window starts/symbol |
| W2 long-symbol bank, T=10.24 s | 0.125 | 0.0094 | 0.035 | 0.0074 | 0.0062 | 0.012 | 0.38 | N=24576, 45056 half-bin freqs, rank 4 subspace, 9 drifts, 4 starts/symbol |
| W3 keyed tiles, guided search | 0.000538 | 4.1e-05 | 0.00015 | 3.2e-05 | 2.7e-05 | 5.3e-05 | 0.0016 | W=25 Hz, 64 dims, 64 data+64 null keyed waveforms, 100 time x 21 freq x 5 drift offsets |
| W3b keyed tiles, blind search | 0.0328 | 0.0025 | 0.0093 | 0.002 | 0.0016 | 0.0033 | 0.099 | 256 time x 500 freq x 5 drift offsets |
| W4 codeword-level correlation, k=20 | 1.01 | 0.076 | 0.29 | 0.06 | 0.05 | 0.1 | 3 | 7.68 s coherent block, 50 residual offsets |
| W4 codeword-level correlation, k=24 | 16.1 | 1.2 | 4.6 | 0.96 | 0.8 | 1.6 | 49 | 7.68 s coherent block, 50 residual offsets |
| W4 codeword-level correlation, k=28 | 258 | 20 | 73 | 15 | 13 | 26 | 7.8e+02 | 7.68 s coherent block, 50 residual offsets |
| W4 codeword-level correlation, k=32 | 4.12e+03 | 3.1e+02 | 1.2e+03 | 2.5e+02 | 2e+02 | 4.1e+02 | 1.2e+04 | 7.68 s coherent block, 50 residual offsets |
| W5 simulation batch, 10^4 x W3b | 328 | 25 | 93 | 20 | 16 | 33 | 9.9e+02 | batch of independent trials; channel synthesis not counted |

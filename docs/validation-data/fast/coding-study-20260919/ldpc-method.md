# Exploratory LDPC benchmark for pumpModem (2026-09-19)

This diagnostic was run outside the application build. Its harness and evidence
are archived here; it does not modify modem behavior.

Source: https://github.com/xdsopl/LDPC at commit 32357d8ad55a6a302c34e093759f0454e45cca56 (0BSD-style permissive license archived as ldpc-upstream-license.txt).
Matrices: DVB-S2 normal length N=64,800 bits, B7 (3/4), B8 (4/5), B9 (5/6), B10 (8/9), B11 (9/10).
Decoder: layered double-precision log-domain sum-product, NormalUpdate, maximum 100 iterations, syndrome stopping; upstream LogDomainSPA clips its phi argument to [1e-6,14.5]. No outer BCH or RS is applied in these runs.

Channel: independent complex AWGN with unit average transmitted constellation energy. N0=10^(-EsN0_dB/10); independent real and imaginary Gaussian noise each has variance N0/2. The harness uses the known N0, exact log-sum-exp bit likelihoods, clipped to [-50,50]. This assumes ideal timing, carrier and channel estimation. It does not simulate pulse shaping, pilots, markers, resynchronization, sound interruptions or transducer distortion.

Modulation: `apsk` copies the ring coordinates and bit labels from src/fast/modem.cpp. `qam` uses unit-energy square QAM with independent reflected Gray mapping on I/Q. A single seeded random permutation of all 64,800 coded bits mixes LDPC variable nodes among constellation bit positions. All rates use newly generated random information and Gaussian noise each frame. A common seed gives repeatable runs; it does not make different-rate data/channel streams identical.

Every decoded code bit is compared with the known transmitted codeword. failed_frames therefore includes both detected and undetected errors; failed_syndrome is the count that reaches the iteration limit without a valid syndrome; undetected_frames is an erroneous codeword accepted as valid. No observed undetected frames does not establish their absence at larger sample counts.

The raw CSV rows preserve every invocation's rate, order, Es/N0, frame count,
seed and geometry. Commands below reproduce any row without the original
temporary batch scripts.
CPU: AMD Ryzen 5 PRO 5650U, 6 cores / 12 threads. Timing is per-process elapsed duration while other independent sweeps may be running; it is diagnostic rather than a controlled single-core benchmark. No SIMD implementation was used.

Confidence caution: zero failures in n trials has a one-sided 95% binomial upper bound of 1 - 0.05^(1/n), approximately 3/n. Small pilot sweeps locate a waterfall but cannot certify the residual block-error rate required by 0.3% sparse outer parity on 5 MB files. Even a successful complete-file-equivalent sample is not a delivery guarantee.

Additional bounded rate search: DVB-S2X normal table B10, K=50,400, rate 7/9; table B20 of the same rate is available as a fallback but was not needed after clean B10 pilots. Final extensions run 800 frames (5,040,000 information bytes per point).

The optional `qam_sep` geometry uses the same square-QAM points, mapping, permutation, random information and AWGN sequence as `qam`. It computes exact I/Q-separable PAM log-sum likelihoods; for a uniform product constellation and independent I/Q AWGN, the other-axis likelihood sum cancels in each bit LLR. It is mathematically equivalent to the full two-dimensional summation. `qam` remains available to reproduce original results. Same-seed checks at 256-QAM and 4096-QAM found the same raw BER, GMI, iteration counts, frame failures and decoded bits (to displayed precision); timing differs. This speeds up the 4096-QAM extension without changing its statistical model or decoder.

To build the final harness from the pumpModem repository root (the temporary
dependency directory must not already exist):

```sh
git clone https://github.com/xdsopl/LDPC /tmp/pump-ldpc-reproduce
git -C /tmp/pump-ldpc-reproduce checkout 32357d8ad55a6a302c34e093759f0454e45cca56
g++ -O3 -march=native -std=c++17 -I/tmp/pump-ldpc-reproduce \
  docs/validation-data/fast/coding-study-20260919/ldpc-benchmark.cpp \
  -o /tmp/pump-ldpc-benchmark
/tmp/pump-ldpc-benchmark 7/9-B10 4096 30.791812 800 719325 qam_sep
```

Invocation: `BINARY RATE ORDER EsN0_dB FRAMES SEED GEOMETRY`.
Geometry is `apsk`, `qam` or `qam_sep`; output is one CSV row without a header.
Use the header from `ldpc-all.csv`. Save reproduction output elsewhere to
preserve the recorded evidence. Only standard C++ and the pinned upstream
headers are needed; the modem library is not linked into this harness.

Final bounded-search result: all three DVB-S2X B10 (7/9) 800-frame extensions completed with zero wrong decoded code bits, zero nonconvergent frames, and zero undetected wrong codewords. They carry 5,040,000 random information bytes each, excluding BCH, outer RS, integrity fields, packet framing and other modem overhead. They are code/channel samples rather than end-to-end application file-transfer tests. Relative to the first coarse winners, their information rates increase 3.7037% at all three SNR/order points. The tested 4/5 matrix fails at all three comparable order/SNR points (32/32 at 256-QAM/20 dB, 16/16 at 1024-QAM/25 dB, 16/16 at 4096-QAM/30 dB), so raising the rate again is not successful for that tested matrix/decoder.

One-sided 95% upper bound after zero failures in 800 independent frames: 0.0037376626. This sample size cannot certify an outer parity budget of only 0.3%, nor does it prove global optimality over LDPC designs, bit mappings, modulation or shaping. It identifies useful fixed-grid candidates for subsequent full modem/noise/interruption qualification.

ldpc-all.csv combines the pilot, supplemental and extended results without double-counting the separate demapper-validation run.

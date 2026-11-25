# Tensor Core reception experiment

This experiment exercises two implementations of an exhaustive receiver for the same fixed repeated-FSK waveform. It measures which messages the receiver recovers, how long the full processing path takes once IQ is available, and how active the Tensor Cores are when they have work queued. It is an isolated experiment for Matrix Modem. The production modem and wire format are separate.

## Receiver and workload

- A fixed bank of 32,768 nonlinear binary codewords represents 15 information bits per opportunity. Each codeword feature picks one of two tones 37.5 Hz apart for two consecutive 80 ms observations. A public cycle of nine nominal centers spans 1.4 kHz. A short frequency transition preserves phase continuity. This is a convenient dense codebook detector for the experiment. FSK itself does not require an exhaustive codebook.
- Complex baseband is sampled at 3,200 samples/s. Both routes score every combination of 64 candidate start times spaced 1.25 ms apart, 32 candidate frequency offsets spaced 18.75 Hz apart, and every codeword. The public opportunity length is fixed. The receiver gets IQ and this calendar, with the message, onset, channel case and actual frequency hidden.
- Both routes use cuFFT to project two adjacent received windows onto each hypothesized tone. Evidence for each tone includes the two powers and, in the history version, the real cross-product of the two complex observations after removing the candidate tone's phase advance. The power-only ablation keeps exactly the same waveform, two observations, energy, candidates and score calculation, with the correlation term removed. There is no waveform-length, energy or GPU-cost comparison to single-copy transmission.
- Let `q+` and `q−` be the Gaussian-model log-likelihood ratios for the two candidate tones. A codeword score is half the sum of `q+ + q−` over features plus half the dot product of `q+ − q−` with the codeword's signs. The 512-feature search assumes a signal-to-noise ratio of 0.8 per look and the 2,048-feature search assumes 0.2. The pair model uses a correlation of 0.35 for history and zero for the power-only control. cuBLAS evaluates the dot products for all rows and words. The FP32 reference uses pedantic FP32. The Tensor Core route uses FP16 dot-product operands and FP32 accumulation, followed by GPU reduction. The cross-product itself is a small FP32 operation.
- For fixed rows with independent complex white Gaussian noise, the individual codeword score is a log-likelihood ratio under the experiment's signal model. The expectation of its exponent under this null is one. The search threshold is `log(rows * words / 0.001)` by Markov's inequality and a union bound. The two tone projections are orthogonal over each 256-sample look. The separate occupancy threshold bounds the probability of unusually large total two-look, two-tone power across any row under the same null. The thresholds make no assumption of independence between overlapping rows. They do not calibrate band-limited, colored, impulsive or drifting-noise interference. The independent tone-power test is a conservative guard for quiet completion. A sufficiently weak signal can still resemble quiet.
- In the stream sequence a decision consumes the entire public opportunity. The harness observes an interior quiet opportunity, a resumed message and two final scored quiet opportunities. Both quiet samples are explicitly searched. Test inputs are independently synthesized finite opportunities.

The primary shape has 512 features, 81.92 seconds of signal time, and 68.72 GFLOP of dense scores per opportunity. The long shape uses 2,048 features, 327.68 seconds of signal time, and 274.88 GFLOP. The long shape trades airtime and payload rate for more observations at low pre-channel C/N₀. These full on-air durations are charged when interpreting the processing latency.

## Reproduce

From this directory, with a CUDA 12.8 toolkit and an SM 120 GPU:

```sh
nvcc -O3 -std=c++17 -arch=sm_120 -Xcompiler -pthread \
  fixture.cpp frontend.cu codebook.cu main.cu -lcufft -lcublas -o matrix_rx
./matrix_rx verify
./matrix_rx suite
```

Build the same receiver with sliding Fourier projections by linking the alternative frontend:

```sh
nvcc -O3 -std=c++17 -arch=sm_120 -Xcompiler -pthread \
  fixture.cpp sliding_frontend.cu codebook.cu main.cu -lcublas -o matrix_rx_sliding
./matrix_rx_sliding verify
./matrix_rx_sliding suite
```

The shorter FFT comparison uses the same 256 IQ samples per look. Every selected bin has an even index in the original FFT. The 512-point variant keeps the original tone projections and uses less zero padding:

```sh
nvcc -O3 -std=c++17 -arch=sm_120 -DMATRIX_RX_SHORT_FFT -Xcompiler -pthread \
  fixture.cpp frontend.cu codebook.cu main.cu -lcufft -lcublas -o matrix_rx_512
./matrix_rx_512 suite
```

On the sliding build, `./matrix_rx_sliding cache-audit` compares full output and 4,096-, 8,192- and 16,384-codeword tiles on the FP16-input scoring route. It checks identical top candidates using the same captured features. It reports scorer wall time without instrumentation and conversion/GEMM/reduction times from separate CUDA-event runs. The scorer replay also accepts a tile width, for example `./matrix_rx_sliding score 8192`. Both cases still evaluate the full codebook.

For local CPU fixture validation, `check_fixture()` in `fixture.cpp` has no CUDA dependencies. To reproduce on another NVIDIA GPU, select an architecture supported by the toolkit and record the actual device.

Run `./matrix_rx score`, `./matrix_rx fp32`, and `./matrix_rx full` in separate windows of device-level telemetry. For example, use `TZ=UTC nvidia-smi dmon --format csv --gpm-metrics 1,2,3,5,7,10,12 -o T -d 1 -c 20`, start the executable after two samples, and use only complete telemetry intervals strictly inside the printed measurement window. `score` and `fp32` replay the same received-signal-derived features and include operand preparation and device argmax. `full` alternates previously generated disturbed-channel signal and white-noise inputs, with transfer, projection, scoring and decisions included every time. It tests processing capacity under queued work. The real radio arrival rate is much slower. GPM reports activity time and dense FLOP/s counts algorithmic score work, so neither number alone is utilization of a theoretical arithmetic peak.

For the separate exact-lag drift probe, which does not depend on the other files:

```sh
nvcc -O3 -std=c++17 -arch=sm_120 drift.cu -lcublas -o matrix_drift
./matrix_drift
```

To run the same fixture, reference comparisons and CPU timing without CUDA:

```sh
g++ -O3 -x c++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -DDRIFT_CPU_ONLY drift.cu -o matrix_drift_cpu
./matrix_drift_cpu
./matrix_drift_cpu --bandwidth
```

The drift probe uses two known-alignment 0.64 s windows, each with eight 80 ms phase-continuous FSK tones. It sweeps 33 absolute carrier offsets and three linear carrier rates in the known-template control, with both a direct calculation and a factored FFT implementation. The history detectors use one alignment and either zero or three relative-frequency corrections. The GPU maps the three complex correlations to a small real GEMM. The trial report checks every factored known-template score against the direct version, all GPU outputs against a complex scalar reference, and includes timing after input is available. See the [drift model and outcome limits](../../../_ref/algorithms-matrixModem/Sol-6.2/received-history-drift.md).

The `--bandwidth` variant reuses that generator. Both arms use the same guard and timing. It compares scoring at 3,200 complex samples/s with a 161-tap low-pass FIR followed by scoring at 400 samples/s. It times filter work separately from scoring and verifies the conditional noise variance and generalized least-squares template normalization. [Bottleneck and bandwidth results](../../../_ref/algorithms-matrixModem/Sol-6.2/tensor-receiver-bottlenecks.md)

## Cases and decision rules

The CPU check is an independent direct DFT and scalar codeword search over a small full grid. The primary grid takes 39 independent seeds, spanning flat, moderate, disturbed, a held-out channel shape with a fractional tone offset, weak disturbed, white-noise, narrowband-interference and out-of-codebook inputs. The long shape uses 16 more, including 12 low-C/N₀ signal inputs. Every trial measures both arithmetic routes and both correlation settings, with alternating timing order between seeds. The fixture uses finite randomized sums of sinusoids, two taps in the dispersive cases, unit expected noise power and a stable public calendar. Small seed counts support an implementation and failure-mode screen.

Report correct word plus accepted presence, false presences, unresolved decisions and decodes reaching quiet incorrectly. A different raw winning timing row is allowed when the data word agrees. The grids overlap heavily at 80 ms window lengths. Record score and arithmetic-bound discrepancies separately. Report scorer and end-to-end wall times separately from steady telemetry. A successful kernel experiment still needs waveform, code, timing-acquisition and physical-channel evaluation before it establishes operational reliability.

The optional conditional-codeword screen recomputes evidence for the already selected FP16 winners using the independent scalar reference. Give it the text emitted by `matrix_rx suite`:

```sh
g++ -O2 -std=c++17 -Wall -Wextra -Werror -pedantic \
  fixture.cpp validate_winner.cpp -o validate_winner
./matrix_rx suite | ./validate_winner
```

The screen quantifies the same-fixture cost of requiring evidence that favors the proposed codeword over independently selected tones. It uses only the nominated row/word and does not change either timed receiver.

The [repeated-FSK receiver results](../../../_ref/algorithms-matrixModem/Sol-6.2/tensor-reception-results.md) give the full timing and decode counts and link the matched drift experiment.

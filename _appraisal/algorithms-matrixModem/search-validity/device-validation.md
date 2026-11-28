# Corrected coherent-search GPU check

## Test design

The two CUDA programs in [sub9-fsk-rate](../sub9-fsk-rate/README.md) exercise the search over tone sums. The physical dimensions and the interpretation of the historical simulation changed after the drift-unit audit. The question for this run is whether the corrected planted input is recovered through each accelerated search route and what fraction of 10-bit words are decoded incorrectly at two fixed energy levels.

- Use one full RTX PRO 6000 Blackwell Server Edition. Record device identity, compiler, executed numerical formats and source hashes.
- For the deterministic search, use a single batch of candidate messages that includes the planted one. Require agreement on the planted message, frequency and drift for FP16, INT8 and cuFFT.
- For the 10-bit simulation, use the strong-signal smoke test first. Then draw 8,192 trials at each of 13 and 15 dB block energy over noise density. Report wrong messages for the known physical hypothesis and the search over the configured grid, with 95% Wilson intervals per marginal rate. The two decisions share the received data. No rate-difference test or stopping based on the rates is planned.
- Stop after these points. The sampled 24-bit search excludes other message batches. The 10-bit result is conditional on the fixed code fixture, physical model and on-grid oscillator hypotheses. Timing acquisition, false presence, varying channel realizations, full calendar service and device-to-device comparisons require additional inputs.

## Observations

Full RTX PRO 6000 Blackwell Server Edition, 188 SMs, 97,887 MiB, compute capability 12.0, MIG disabled. Driver 595.112.07, CUDA compiler 12.8.93, cuBLAS 12.8.4.1, cuFFT 11.3.3.83. Host was an EPYC 9355, with a cgroup CPU quota of 13.6 CPUs and a memory limit of 250,999,996,416 bytes. The first non-login SSH shell could not resolve `nvcc` and ran zero assertions. The completed run used the installed CUDA toolchain through the explicit path below.

The 4,096-message batch within the 24-bit code space recovered planted message `7f3a79`, printed frequency index −21 and drift index 2. FP16-input GEMM with FP16 and FP32 outputs, INT8-input GEMM with INT32 output, and the cuFFT route each returned that same triplet on the whole-block rung. The 10-bit strong-signal test recovered all 128 messages for both decisions.

| Block E/N₀ | Known frequency and drift, wrong / 8,192 | Error rate (95% interval) | Searching 696 hypotheses, wrong / 8,192 | Error rate (95% interval) |
| --- | ---: | ---: | ---: | ---: |
| 13 dB | 91 | 1.111% (0.906–1.362%) | 1,257 | 15.344% (14.580–16.141%) |
| 15 dB | 0 | 0% (0–0.047%) | 49 | 0.598% (0.453–0.790%) |

At 13 dB, knowing the oscillator hypothesis brought the error rate close to 1%. Searching the same data over the grid produced an error rate above 14% at the lower interval endpoint. Both rows score all 1,024 possible messages. The Monte Carlo plants tones using rectangular-window projections at all 16 positions with frequency evaluated at the tone midpoint. Each trial uses one on-grid frequency and drift, one timing alignment, a single specular path and Gaussian noise. The scored window is the whole block. Public-seed surrogate schedules and quantized 4-bit bin components limit transfer to an operational receiver. The two fixed points characterize neither the exact 1% threshold nor timing acquisition.

Each energy point took approximately 0.11 s of warm GPU trial-loop wall time (roughly 78,000 trials/s) batched by 128. Setup, compilation, sample conditioning and quiet-slot service lie outside that timing. Actual Tensor Core utilization was not instrumented in this run.

## Reproduction

From the [source directory](../sub9-fsk-rate/README.md) on the full CUDA device:

```sh
export PATH=/usr/local/cuda/bin:$PATH
nvcc -O3 -std=c++17 -arch=sm_120 search_gpu.cu -lcublas -lcufft -o /tmp/search_gpu
nvcc -O3 -std=c++17 -arch=sm_120 sim_gpu.cu -lcublas -o /tmp/sim_gpu
/tmp/search_gpu smoke 512 174 1
/tmp/sim_gpu smoke
/tmp/sim_gpu two-point
```

| Source or executable | SHA-256 used on the device |
| --- | --- |
| search_geometry.hpp | `f5e64a43ea6d6d4c8f3cc2252c75a03caeaa9c6518f4d45bee574bce06ff4145` |
| search_gpu.cu | `797e919f66efd77cbdf0db92bb3fa825149df83bddae6074ae63f34cf978b601` |
| sim_gpu.cu | `00e995305250d6b982458a2384cca78121458832b26fc45f74bb787b5de0364e` |
| search_gpu | `9dae42490ea3585db1c9f6dbceac0689651f1d388d1fc3f51dfd655f36547f50` |
| sim_gpu | `7f3080d064e49175be80f2f2669e3559795a996e384dc007798355df89892808` |

The [previous CUDA output](../sub9-fsk-rate/results/gpu/sim_gpu.out) used another drift convention and directly planted signal on a single bin. It remains a record of that earlier fixture.

After device release, `search_gpu.cu` added a clamp of the starting high-half index to keep nondivisor batch widths within the 24-bit space. For this smoke `CB=1`, so the maximum possible old index equals the new cap, 4,095. Its bounds are identical for every planted word in this invocation. The GPU ran the source hash in the table. The simulator and geometry header match the sources used for the reported counts.

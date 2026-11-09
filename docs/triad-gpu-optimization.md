# Retained GPU optimization lessons

Follow [precision controls](gpu-precision-controls.md) and the
[current abbreviated policy](triad-performance.md). The former frozen full-run
optimization ladder, sensitivity sweeps and calibration campaigns are retired.
There is one canonical receiver implementation; alternative arithmetic belongs
inside the canonical implementation. The historical precision controls and
GPU receiver APIs discussed in these reports have been removed; this page is
research context, not a current precision-control contract.

## What to measure separately

Bulk template/correlation work, preparation, transfers, RNG/channel generation,
small factorizations and reductions can have very different costs. A fast kernel
is not end-to-end useful receiver throughput. Preserve input, source, shape,
precision, cache and device identities; same-device and cross-device comparisons
answer different questions. Advertising tensor peaks is not measuring this work.

Lower/tensor arithmetic is worth exploring where bulk work dominates. Sensitive
Gram/rank, covariance and likelihood reductions may benefit from higher precision
at little total cost. Do not hide expensive FP64 preparation behind a low-precision
selection or downcast every stage indiscriminately. Ill-conditioned, rank-deficient
and near-admission-boundary cases need explicit fallback and uncertainty.

Caching templates, FFTs, Grams and bases; shared observations; persistent buffers;
FFT proposals with sparse refinement; fused reductions; and suitable matrix
shapes are reusable ideas. Their value depends on preparation reuse and the real
workload. Search pruning must retain honest coverage and cannot count omitted
hypotheses as completed work.

One retired receiver experiment on an RTX 4090 found FP16 replay slower than
strict FP32, while joint FP8 quantization added misses in a 96-capture sample.
These are useful counterexamples, not predictions for the current receiver,
different hardware or different inputs.

## Historical findings and limits

The October 31–November 1, 2026 studies used retired isolated v3 receivers,
including the earlier cyclic-amplitude/QPSK source. The optimization baseline
was `75f3af291100d513a8bc20bff9f26d259b51ed71`; the tensor and FP4 follow-ups
started from `ebfe649c1f9c0a5bace48594c982111f64c28f41` with recorded local
changes. These are dated algorithm findings, not current-source qualification.

| Historical comparison | Useful result and boundary |
| --- | --- |
| Bulk and reduction precision | RTX 5090 fixed-hour B1 inference took 0.202 s with strict FP32 bulk versus 0.704 s with FP64. A different two-level FP64 reduction organization improved surrounding inference about 1.15× by changing data movement, without lowering that reduction's precision. Neither comparison covered full acquisition or sensitivity equivalence. |
| Cached preparation, fused updates and graph replay | Equal-work processing improved 4.62× and complete subprocess time 3.82×. At the sampled HF-hour point, reception was 277/300 at the default gate and 295/300 at a relaxed gate; operational false admission remained unqualified. |
| Lower-precision phase-state contraction on RTX 5090 | Plain BF16, TF32 and scaled FP16 failed the predeclared likelihood-error/bias screen despite unchanged decisions in the small screen. Split BF16 and TF32x3 passed that development screen and improved resident inference about 3.5% and 0.7%; complete-job medians were near parity. |
| Native FP4 correlation | On a large synthetic GEMM, NVFP4 was about 9.5× faster than FP32 including packing. The actual fused correlation batch was 24.04/17.85 ms for FP32/NVFP4 on PRO 6000, but 29.16/38.95 ms on RTX 5090. A cached small complete receiver on RTX 5090 took 13.21 ms with FP32 versus 14.01 ms with FP4. Device, shape and surrounding work determined the useful result. |
| Consumer tensor arithmetic | RTX 4090 fixed-capture replay achieved 63.15 captures/s with strict FP32 and 61.71 with FP16; larger copied FP16 batches reached 47.20. Joint FP8 added misses in the separate 96-signal-capture sample. These replays were not independent reception trials or continuous-streaming qualification. |
| Scaling and representation | A small FP64 restoration epilogue corrected subnormal handling while bulk products remained lower precision. FP32 partial sums could reduce accumulation loss without FP64 outer sums. FP64 arithmetic on stored complex64 templates did not create an independently generated FP64 waveform reference. |

Quantized-observation likelihoods, unknown interference and operational tails
were not qualified. The 0.1/0.25/0.5/1 dB sensitivity-loss budgets remained
unresolved, including hour/day misses shared by both reference and quantized
paths. Hardware utilization counters were unavailable; kernel timings and
instruction inspection did not establish SM occupancy or memory bandwidth.
These limits concern those historical models, not a general need for high
dynamic range in the current receiver.

The superseded raw archives, plots, copied logs and precision evidence catalog
were removed after distilling these findings and the
[noise-model lesson](triad-noise-calibration.md). No current validation or
release claim depends on those campaigns. Follow the
[material lifecycle](development.md#material-lifecycle) rather than restoring
their drivers, source copies or exhaustive run instructions.

New intensive experiments must predeclare small distinct-path strata, confidence
and stopping rules, and applicable extrapolation. Unsupported prediction remains
unresolved. RunPod may shorten this same abbreviated method; it does not restore
the old full-workload objective or authorize larger campaigns.

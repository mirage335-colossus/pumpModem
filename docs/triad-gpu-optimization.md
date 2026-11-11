# Retained GPU optimization lessons

Follow [precision controls](gpu-precision-controls.md) and the
[current abbreviated policy](triad-performance.md). The former frozen full-run
optimization ladder, sensitivity sweeps and calibration campaigns are retired.
There is one canonical receiver implementation; alternative arithmetic belongs
inside the canonical implementation. The historical precision controls and
standalone GPU receiver APIs discussed in these reports have been removed.
This page records their evidence and [native disposition](#native-disposition-of-the-historical-techniques);
the current options and qualification rules are in the
[native precision guide](gpu-precision-controls.md).

**Successful Tensor Core DSP implementations exist in Git history.** NVFP4
accelerated an actual large correlation batch on RTX PRO 6000; split-BF16 and
TF32x3 accelerated a phase-state workload on RTX 5090. The recent small FP16
Gram failure does not negate those results. Their arithmetic, layout and reuse
techniques deserve consideration inside the current native backend, with new
qualification for its waveform, shapes and complete work.

**Preserve the hardest-search computational ceiling when selecting defaults.**
The canonical native backend now contains optional FP16 and INT8
post-channelization lag-DFT paths with reusable coefficients and bounded
workspace/arrival batching. Bounded GPU fixtures and two large bulk shapes pass;
complete-receiver qualification remains separate. The new FP4 candidate was
removed after failing the then-frozen noise-peak gate, preserving its findings.
That historical diagnostic failure alone is not a universal precision veto:
current qualification compares complete cost, physical reception, false alarms
and measured sensitivity loss with the user's proportional tradeoff in the
[precision guide](gpu-precision-controls.md). CUDA full frequency-tile and
chirp/zoom FFT coverage remains the default: a dense matrix's sample-by-frequency
arithmetic growth does not disappear when its storage is tiled. The qualified
Radeon direct route has a separately bounded envelope. Promote a different
default only with applicable complete-work and backlog evidence; an easy-case
efficiency result is insufficient.

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

## Verified Git history inventory

The 10 November 2026 audit searched the 757 locally reachable commits
(10 September–10 November), including deleted paths and available branches/tags,
then inspected the relevant implementation lineages and selected original
receipts. Audit head: `89945e838bbbc7fbc259000cee9e401e7c804c63`. This is a
focused inventory of measured or promising wins, not a claim to have tested
every historical revision. No old receiver was executed or restored.

The October 31–November 1, 2026 studies used retired isolated v3 receivers,
including the earlier cyclic-amplitude/QPSK source. The optimization baseline
was `75f3af291100d513a8bc20bff9f26d259b51ed71`; the tensor and FP4 follow-ups
started from `ebfe649c1f9c0a5bace48594c982111f64c28f41` with recorded local
changes. These are dated algorithm findings, not current-source qualification.

| Historical implementation and evidence | Useful result and boundary |
| --- | --- |
| **NVFP4 correlation, resident geometry and fused reconstruction**, stored in [`25b320a2`](https://github.com/mirage335-colossus/pumpModem/blob/25b320a2/docs/validation-data/triad-v3/2026-11-01-fp4/README.md#L21) | Full PRO 6000 actual correlation batch: strict FP32 **24.039896 → 17.852368 ms, 1.347×**. Same input hashes and 68,640 hypotheses/capture; includes packing, reconstruction, Grams and assembly, excludes phase inference/admission. The same large batch on RTX 5090 favored FP32, **29.16 versus 38.95 ms**. A separate RTX 5090 synthetic 8192×8192×4096 GEMM improved **9.475× including both packs**; that is not modem throughput. Small complete RTX 5090 receiver also favored FP32, 13.21 versus 14.01 ms. |
| **Fused split-BF16 / TF32x3 phase-state contractions**, [`25b320a2`](https://github.com/mirage335-colossus/pumpModem/blob/25b320a2/docs/validation-data/triad-v3/2026-11-01-tensor/README.md#L34) | RTX 5090 resident inference **8.41737 → 8.12849 / 8.35953 s**. With setup amortized over 32 batches, seven-SNR-point complete subprocess medians **39.25711 → 37.74718 / 38.76491 s**, **1.040× / 1.013×**. Numerical screens and sampled rankings/gates passed. Fixed bank; physical generation, blind acquisition and operational sensitivity tails were not qualified. These sequential workload medians are not timing confidence intervals. |
| **Fused FP16 packing and tiled products**, [`933cdfe9`](https://github.com/mirage335-colossus/pumpModem/blob/933cdfe9/docs/validation-data/triad-v3/2026-11-01-consumer-gpu/README.md#L68) | RTX 4090 synthetic 2048×1024×4096: FP32 **0.423776 → 0.284672 ms**, **1.49× including both packs**; cached FP16 product 0.175104 ms. Complete receiver replay favored FP32, **63.149 versus 61.708 captures/s**. Packing/layout is a useful technique; a universal precision win is not established. |
| **K32 tensor partials with explicit FP32 outer additions**, [`933cdfe9`](https://github.com/mirage335-colossus/pumpModem/blob/933cdfe9/docs/validation-data/triad-v3/2026-11-01-consumer-gpu/README.md#L52) | Represented FP8 arithmetic error improved **21.85× / 26.62×** for E4M3/E5M2 at approximately unchanged 0.15 ms kernel time. FP64 outer accumulation gave the same sampled answers about **6.3× slower**. This controls product accumulation, not input quantization or weak-signal sensitivity. |
| **Scaled FP16 lag-domain DFT**, recorded in [`247b7202`](https://github.com/mirage335-colossus/pumpModem/blob/247b7202/docs/validation.md#L352) | PRO 6000 isolated transforms improved **1.88× / 1.33×**, including scaling/packing/setup/rescaling; paired receiver precision/work gates passed. Acquisition increased **3.543 → 4.103 s** and common stages were effectively unchanged. A transform win, not an integrated win. Experimental source was removed before this documentation commit; retained source hashes are not recoverable code. |
| **Fused phase stencil, persistent CUDA graphs/buffers and register reducer**, [`ebfe649c`](https://github.com/mirage335-colossus/pumpModem/blob/ebfe649c/docs/validation-data/triad-v3/2026-11-01-optimization/hardware.md#L8) | Matched RTX 5090 B4, 192 observations: processing **47.4876 → 11.7625 s, 4.04×**; whole subprocess **50.4042 → 14.9011 s, 3.38×**. Rankings/bits/admission matched; maximum log-LR difference 5.66e-6. B12 produced **4.62× / 3.82×**, but changed batch-shaped noise ordering: equal counts, not matched-input parity. This stencil used ordinary FP32/FP64, not Tensor Cores; acquisition and physical generation were excluded. |
| **Persistent whole-scan graph workspace**, [`25b320a2`](https://github.com/mirage335-colossus/pumpModem/blob/25b320a2/docs/validation-data/triad-v3/2026-11-01-fp4/README.md#L55), `tools/triad_scan_workspace.py` | Graph-captured correlation plus inference over all 85 blocks: RTX 5090 FP32 **37.00→13.21 ms, 2.80×**; NVFP4 **56.08→14.01 ms, 4.00×**; PRO 6000 NVFP4 **20.50→16.12 ms, 1.27×**, each versus its eager counterpart. Includes fresh-input copy, reset, scoring, D2H scores and CPU gates; excludes cold preparation. A/zero-B/A and random A/B/A reset checks passed. Fixed clock/key/geometry; no moving-clock or arbitrary workspace-reuse qualification. |
| **Mixed precision and FP64 reduction organization**, [`ebfe649c`](https://github.com/mirage335-colossus/pumpModem/blob/ebfe649c/docs/validation-data/triad-v3/2026-11-01-optimization/README.md#L110) | RTX 5090 full-hour graph inference **0.704 → 0.202 s, 3.49×** with FP32 bulk/FP64 evidence rather than all-FP64 bulk. Separately, two-level FP64 reduction avoiding a full converted temporary improved **0.787498 → 0.683743 s, 1.152×** with score error ≤4.263e-14. Neither covers complete acquisition or sensitivity equivalence. |
| **Prepared Gaussian design and simulator caches**, [`ebfe649c`](https://github.com/mirage335-colossus/pumpModem/blob/ebfe649c/docs/validation-data/triad-v3/2026-11-01-optimization/README.md#L85) | Synthetic B32 receiver cache: **3.78× / 4.06× resident**, **1.64× / 1.73× including design/transfer/verification**, FP32/FP64 respectively. Separate simulator preparation **0.32935 → 0.07931 s, 4.15×**, with 4,645 bit-identical blocks; five-use amortization 3.21×. Simulator truth never becomes receiver input. Warm CPU single-use receiver cache was slower (0.954×). |
| **Cached direct-GEMM acquisition bank**, [`ebfe649c`](https://github.com/mirage335-colossus/pumpModem/blob/ebfe649c/docs/validation-data/triad-v3/2026-11-01-optimization/hardware.md#L158) | Complete small 288-hypothesis bank ×128 observations: PRO 6000 kernel **4.246 → 0.868 ms, 4.89×** versus FFT; RTX 5090 **5.30×**. Full-family winners/gates agreed; preparation/verification dominated the complete pipeline. Shape-specific alternative, not broad-search or synchronization qualification. |
| **Independent trial sharding across two GPUs**, [`ebfe649c`](https://github.com/mirage335-colossus/pumpModem/blob/ebfe649c/docs/validation-data/triad-v3/2026-11-01-optimization/hardware.md#L137) | Two PRO 6000s achieved **1.960–1.981× complete-job speedup** with bit-identical saved scores. Useful for accelerating independent qualification trials, not evidence of parallelizing one received stream. |

The FP4 and phase-contraction results have more than dtype labels as mechanism
evidence. The inspected FP4 source calls `tl.dot_scaled`, checks the compiled
dense FP4 MMA path and rejects fallback; archived SASS contains
`OMMA.SF.16864.F32.E2M1.E2M1.UE4M3.4X`. Fused phase-contraction SASS contains
`HMMA.16816.F32.BF16` and `HMMA.1688.F32.TF32`. Source hashes match the profiled
files and numerical output receipts. Here “native FP4” means compiled GPU
instructions launched through Python/Triton, not the current C++ backend.
The [Triton operation](https://triton-lang.org/main/python-api/generated/triton.language.dot_scaled.html)
can emulate unsupported formats, so actual instruction/fallback evidence matters;
see the [NVIDIA MMA reference](https://docs.nvidia.com/cuda/parallel-thread-execution/#warp-level-matrix-instructions-mma).

Stored commits and measured source identities are distinct. FP4 reports name
`ebfe649c + source-hashed-study`; the three measured implementation hashes match
the files stored in `25b320a2`. The final phase-contraction source hash also
matches `25b320a2`. The graph source matches `ebfe649c`; its measured FP64 reducer
uses the archived `08c3446f…` source snapshot, with later dtype/accounting guards
in the committed file. Source, raw receipts and selected assembly were inspected
directly from Git objects without restoring campaigns.

## Native disposition of the historical techniques

All eleven GPU groups above have a current disposition below. Together with
the [ten CPU groups](triad-performance.md#earlier-cpu-implementations-worth-reconsidering),
the audit accounts for 21 historical technique groups. “Implemented”
describes code in the canonical native stack, not qualification by the old
measurement. Current scoped results and unresolved gates are in
[validation](validation.md#native-matrix-and-preparation-follow-through). The old finite phase-state model
and cyclic-amplitude/QPSK waveform differ from the current receiver, so retain
the transferable technique rather than restoring another receiver.

The table retains original numerical thresholds, score errors and changed peak
findings as historical observations. A score or peak difference alone does not
quantify operational sensitivity loss or establish that a different low-precision
method is inadmissible. Current evaluation must retain every public hypothesis
and measure physical outcomes, false alarms, uncertainty, complete cost and the
user's sensitivity-versus-computation tradeoff.

| Historical group | Current native disposition |
| --- | --- |
| NVFP4 correlation and fused reconstruction | The historical Triton result remains a proven scoped win. A new native cuBLASLt analogue was removed: single-product FP4 failed the 1% score gate (5.03% error), and four-product/residual-scale repairs changed a unique noise peak. Active bulk costs improved against FFT but did not establish an advantage over qualified INT8. This does not reject other FP4 designs or transfer the historical result to current C++. |
| Split-BF16 / TF32x3 phase contractions | These arithmetic modes remain absent. The old phase-state contraction does not map directly onto current Gaussian tracking. The high/residual principle was tested in the removed FP4 analogue, using actual decoded residuals, HH/HL/LH/LL products and FP64 reconstruction. Its noise-peak failure leaves the historical split-BF16/TF32x3 findings intact. |
| Fused FP16 packing and tiled products | Native scaled packing and FP32-accumulating products implemented; include all packing, setup and reconstruction when qualifying the path. The historical complete-receiver loss remains relevant. |
| K32 partials and explicit outer accumulation | Bounded-partial principle implemented with complex chunks of at most 512 samples, INT32 sums for INT8 and FP64 outer reconstruction. This is different arithmetic from the historical FP8 K32 result and needs its own numerical gate. |
| Scaled FP16 lag-domain DFT | Shape-qualified native coarse lag-group analogue implemented after DATA channelization, with shared coefficients and full FFT fallback. Historical transform wins did not establish an integrated win. |
| Fused phase stencil, graphs and register reducer | Current fused mapping, direct small Gram reduction and coalesced readback preserve fusion/reduction principles. Exact CUDA arrival-peak frontiers pass device fixtures and the full 8,596-frequency active/noise public-slot checks; a bounded bulk test measures the transfer/selection improvement. Sustained backlog and CPU forecasting remain separate. The old stencil belongs to a different model; CUDA graph replay is still absent. |
| Persistent whole-scan graph workspace | Exact descriptor and source-tile caches preserve part of the residency lesson; they are not graph replay. Bounded post-render projection/statistics capture remains an unimplemented candidate. |
| Mixed precision and FP64 reduction organization | FP32 bulk with FP64 coordinates, statistics and posterior evidence is retained. The historical two-level reducer is not established as the same current implementation, so its ratio does not transfer. |
| Prepared Gaussian design and simulator caches | Persistent native mapped bases, projection plans and device tiles are retained. Exact projection/offset descriptor reuse and shared block-pair transition preparation extend this lesson. Simulator truth remains outside receiver input. |
| Cached direct-GEMM acquisition bank | Prepared native lag-group matrices and batching across workspaces/arrivals provide a shape-specific analogue. They evaluate every public hypothesis and do not replace broad FFT acquisition with an unbounded dense bank. |
| Independent two-GPU trial sharding | Still applicable to independent compiled qualification jobs when needed. It is test orchestration, not one-stream receiver parallelism; it does not justify a second modem implementation. |

Current matrix batches can combine up to 32 workspaces, 64 arrivals and two taps
into 4,096 complex rows, subject to ready work and full memory admission. This
addresses a material historical distinction: useful large contraction shapes
must not be judged solely from the current fine stage's 1×1 or 2×2 Gram matrices.
Receipts expose actual dimensions and execution/fallback counts; potential
maximum shape alone is not evidence that a test dispatched it.

An optional split-BF16/TF32x3 variant would fit the prepared coarse matrix
boundary, subject to supported library/device shapes, explicit complex packing,
all split-product costs and scratch admission, complete public work and current
physical/sensitivity qualification. Historical score gates remain diagnostics,
not universal sensitivity criteria. It cannot replace the current Gaussian posterior or FP64 state by analogy
with the old phase lattice. The removed split-FP4 experiment informs that
residual technique without establishing those absent arithmetic modes.

The next graph boundary would be fixed-shape projection/statistics after native
rendering. It needs stable buffers, exact geometry/precision/bin signatures,
fresh observation and witness reset, warmed FFT plans, coordinated capture
across owners, and full graph-pool lifetime accounting. Callback-driven source
generation, changing keyed tiles and blocking readback cannot simply be captured
as though their host control flow replayed. Descriptor caching is a prerequisite
for some of this work, not proof that the historical graph gains are retained.

See the separate [CPU history inventory](triad-performance.md#earlier-cpu-implementations-worth-reconsidering)
for preserved native kernels, old waveform-specific methods and remaining reuse
opportunities. The current native precision guide keeps
[arithmetic and CPU forecasting limits](gpu-precision-controls.md#qualifying-a-faster-path)
separate from this historical inventory.

## Historical numerical limits and lifecycle

Plain BF16, TF32 and scaled FP16 phase-state contractions failed the original
likelihood-error/bias screen despite unchanged decisions in a small sample.
Joint FP8 observation quantization added misses in the separate 96-capture
sample. These failures do not invalidate the qualified split-product or FP4
arithmetic results above. A small FP64 restoration epilogue corrected subnormal
handling while bulk products remained lower precision; doing FP64 arithmetic
on stored complex64 templates never created an independent FP64 waveform.

Quantized-observation likelihoods, unknown interference and operational tails
were not qualified. The 0.1/0.25/0.5/1 dB sensitivity-loss budgets remained
unresolved, including hour/day misses shared by both reference and quantized
paths. Hardware utilization counters were unavailable; kernel timings and
instruction inspection did not establish SM occupancy or memory bandwidth.
These limits concern those historical models, not a general need for high
dynamic range in the current receiver.

Most implementations were removed in `419e6bae` during canonical-stack
consolidation, not because every measured win was disproved. The prepared
Gaussian Python implementation survived until `6bc9b0e3`'s Python retirement.
The superseded raw archives, plots, copied logs and precision evidence catalog
were removed after distilling these findings (including `345e8d70`) and the
[noise-model lesson](triad-noise-calibration.md). No current validation or
release claim depends on those campaigns. Follow the
[material lifecycle](development.md#material-lifecycle) rather than restoring
their drivers, source copies or exhaustive run instructions.

New intensive experiments must predeclare small distinct-path strata, confidence
and stopping rules, and applicable extrapolation. Unsupported prediction remains
unresolved. RunPod may shorten this same abbreviated method; it does not restore
the old full-workload objective or authorize larger campaigns.

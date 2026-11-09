# GPU precision guidance

This page summarizes GPU precision experiments on a retired single-carrier
receiver. Its Search arithmetic dropdown, receiver APIs, and standalone
precision switches are no longer current interfaces. The recorded measurements
apply only to the cited source, hardware, inputs, and workload; they are not
current Robust, Fast, or physical-link qualification.

The [GPU optimization summary](triad-gpu-optimization.md) consolidates the
transferable stage-cost, arithmetic and negative-result lessons with their
limits. Follow the [material lifecycle](development.md#material-lifecycle)
instead of retaining redundant captures or old implementations.

The current bounded RunPod diagnostic is not a qualified Robust GPU receiver or
a user-selectable precision control. On an RTX PRO 6000, bulk CUDA coarse-data
and continued-fine work used FP32; small Gram/Gaussian decision work used FP64.
The run measured 2.11–14.71x coarse-data and 1.73–2.33x continued-fine stage
speed versus the pod's 12-worker CPU, while end-to-end native bank processing
was only 1.01–1.32x. Three paired CPU-reference gates passed. These results
support faster bounded GPU diagnostics, not complete GPU reception, a local CPU
estimate or a precision fallback contract.

FP32 remains a measured diagnostic choice within these tested shapes and
decisions. Preserve explicit numerical agreement and fallback reporting for
new work; small score changes can alter rankings and downstream decisions.
Same-row nominal CPU forecasts for rows 3–8 passed; rows 1–2 were rejected.
Exact-cap row-7 transfer forecasts also matched nominally, but the cold cache
hit gate and hybrid acquisition timing gate remain unresolved. Do not convert
GPU speed into a general CPU estimate. See the
[validation record](validation.md) for report paths and claim limits.

New intensive Robust checks follow the
[abbreviated statistical policy](triad-performance.md). GPU stage timing does
not by itself estimate full receiver latency or physical performance.

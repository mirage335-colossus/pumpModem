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

The bounded RunPod forecast uses CUDA for workload observation. The driver
records the actual coarse and fine arithmetic and requires paired CPU evidence
for each fine precision class it forecasts. Bulk fine work normally uses
complex64. The compact tight-clock class uses complex128 where small ranking
changes affect downstream work. Gram and Gaussian state calculations retain
their explicit high-precision paths. The headless CPU receiver has separate
fixed arithmetic.

Keep candidate order, decision thresholds, state work and precision selection
in paired checks. Small score changes can alter rankings and downstream
decisions. The earlier RTX PRO 6000 run rejected rows 1–2 and left the row-7
hybrid timing gate unresolved under its relative-only rule. See the
[validation record](validation.md) for report paths and measured claim limits.
Use source-matched local phase costs for CPU forecasts. Follow the
[GPU-assisted procedure](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast).

New intensive Robust checks follow the
[abbreviated statistical policy](triad-performance.md). GPU stage timing does
not by itself estimate full receiver latency or physical performance.

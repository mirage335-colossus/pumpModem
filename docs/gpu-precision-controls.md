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

New intensive Robust checks follow the
[abbreviated statistical policy](triad-performance.md). Its bounded kernel
forecast does not estimate full receiver latency or physical performance.

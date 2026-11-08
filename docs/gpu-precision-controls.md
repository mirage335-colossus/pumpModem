# Historical GPU precision research

This page summarizes GPU precision experiments on a retired single-carrier
receiver. Its Search arithmetic dropdown, receiver APIs, and standalone
precision switches are no longer current interfaces. The recorded measurements
apply only to the cited source, hardware, inputs, and workload; they are not
current Robust, Fast, or physical-link qualification.

## Retained findings

- Stage costs differ. A faster multiplication kernel or inference pass does not
  establish faster acquisition or end-to-end reception. Preparation, caching,
  transfers, reductions, and final decisions must be measured in the relevant
  workload.
- Lower precision is not automatically faster or equivalent. On the cited RTX
  4090 workspace, FP16 replay throughput was below strict FP32; joint FP8
  quantization added misses in the reported 96-capture sample. These historical
  observations do not predict current hardware or algorithms.
- Improving data movement and fusing reductions sometimes helped more than
  changing reduction precision. Small sensitive operations and large bulk
  products need separate cost and accuracy evidence.
- Reuse changes the economics of preparation. Report cold and repeated-use
  costs separately, and bind each result to the exact cache identity and input
  representation.

The [GPU optimization research summary](triad-gpu-optimization.md) and its
[dated experiment records](validation-data/triad-v3/2026-11-01-optimization/README.md)
retain source-bound findings, including failed cases and limits. Archive
retirement is recorded in
[`archive-content-retirement.json`](validation-data/triad-v3/archive-content-retirement.json);
retained measurements are historical data, not current executable recipes.

New intensive Robust checks follow the
[abbreviated statistical policy](triad-performance.md). Its bounded kernel
forecast does not estimate full receiver latency or physical performance.

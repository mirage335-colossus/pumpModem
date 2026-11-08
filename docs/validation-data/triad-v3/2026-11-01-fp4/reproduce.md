# 1 November 2026 FP4 study record

This page is a concise historical record, not a reproduction procedure. The GPU campaign, archived source snapshots, frozen inputs and run outputs are not routine test infrastructure. Follow the current [computationally intensive Robust testing policy](../../../robust-current.md#testing-computationally-intensive-robust-changes) and [abbreviated triad harness](../../../triad-performance.md) for new work; do not extend or replay this full campaign.

## Purpose and transferable findings

The isolated study compared strict FP32, FP64 and native block-scaled FP4 correlation across sampled triad receiver workloads. Native FP4 accelerated some large matrix operations, but packing-inclusive correlation and whole-receiver gains were much smaller. In the fixed 36-hypothesis workspace case on RTX 5090, median complete compute times were 13.21 ms for strict FP32, 14.01 ms for native NVFP4 and 14.99 ms for FP64. Thus FP4 was slower than strict FP32 in that workload. Six sampled A/zero-B/A checks reproduced baseline scores and decisions; these checked state reset, not independent reception or noise-tail calibration.

The broader frozen holdout contained 753 sampled captures and 1,506 paired policy evaluations. It recorded 0/128 null admissions and 0/32 wrong-key admissions; these samples are too small to establish operational admission bounds. At the −8 dB-Hz target point, both policies recovered 320/320 captures, but paired confidence limits and the frozen design do not resolve the 0.1, 0.25, 0.5 or 1 dB sensitivity budgets. Quantized reception remains uncalibrated and no reduced-precision policy was selected as a default. GEMM microbenchmarks and repeated timings are not independent reception trials or qualified useful-result throughput.

The study also supports retaining complete declared search accounting, paired inputs for numerical comparisons, and explicit distinction between cold preparation, resident compute and complete-job timing. Native FP4 labels were backed by generated instruction evidence in the original study; API success or FP32 output alone is not evidence that hardware executed FP4.

## Provenance and limits

The [campaign summary](README.md) and [frozen comparison protocol](protocol.md) describe the measured scope and assumptions. The archived record is identified by [SHA256SUMS](SHA256SUMS), and its compact analysis by [analysis/analysis.json](analysis/analysis.json) and [analysis/workspace-summary.json](analysis/workspace-summary.json). The study used isolated sources and sampled inputs; it did not integrate or qualify the ordinary Robust receiver. Hashes identify those historical artifacts and do not make them current build inputs or a reason to retain regenerated copies.

No replay is needed to apply these findings. When a concrete current question remains, use a small predeclared comparison with minimal representative strata, matched source/configuration/input provenance, an uncertainty decision and a stopping rule. Preserve only evidence needed for that unresolved question; remove temporary outputs and cloud storage when it is resolved. See the maintained [material lifecycle policy](../../../development.md#material-lifecycle).

# 1 November 2026 GPU optimization study record

This page records what can be transferred from the isolated triad GPU study. It is not a runbook. The historical commands, frozen source overlays, sampled inputs and raw campaign outputs are not maintained as routine reproduction infrastructure. Use the current [Robust testing policy](../../../robust-current.md#testing-computationally-intensive-robust-changes) and [abbreviated triad harness](../../../triad-performance.md) for new work; do not extend this historical campaign.

## Purpose and findings

The study compared reference and optimized GPU organizations for a fixed three-carrier likelihood workload. It found that cached preparation, fused phase-state updates and CUDA graph replay can reduce conditional inference time. The recorded equal-work result was 4.62× for the processing pipeline and 3.82× for the complete subprocess job. A separate 300-path sampled HF-hour holdout recovered 277/300 captures at the default gate and 295/300 at the relaxed gate. These are profile-specific reception observations, not operational qualification or false-admission calibration.

The paired precision screens did not resolve the 0.1, 0.25, 0.5 or 1 dB comparison budgets, and no reduced-precision default was selected. The sampled ADC screen found small projection perturbations at tested 16-bit high-headroom settings; lower full scale and interference produced clipping or overload. Those fixed-path measurements do not establish multi-path sensitivity. The ideal ADC bound applies only to its stated unclipped, fixed, rank-one, known-window assumptions. Nsight Compute hardware counters were unavailable, so utilization, achieved memory traffic and occupancy were not measured.

## Evidence provenance and limits

The accompanying [study summary](README.md), [hardware measurements](hardware.md), [precision notes](precision-stages.md), and [pre-optimization protocol](../../../triad-gpu-optimization.md) contain the retained conclusions and scope. The historical archive is hash-indexed by [evidence-files.sha256.json](evidence-files.sha256.json), [source-files.sha256.json](source-files.sha256.json), and [artifacts.sha256.json](artifacts.sha256.json); these identify recorded artifacts but do not make them current test inputs or qualify the current implementation. The study used isolated sources and fixed exported inputs, and was not integrated into the ordinary Robust receiver.

No further replay is required to use these findings. If a current engineering question needs one of them, design a small, predeclared comparison under the maintained abbreviated-testing policy, record matched source/configuration/input provenance and uncertainty, then remove temporary outputs when that question is resolved. Do not preserve new campaign copies, source trees, GPU volumes or run outputs without a concrete unresolved purpose.

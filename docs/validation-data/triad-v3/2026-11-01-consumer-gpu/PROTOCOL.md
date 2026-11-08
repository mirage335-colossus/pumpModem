# Consumer GPU tensor-correlation study record

This page records algorithm and measurement lessons from the isolated RTX 4090 study. It is not a protocol for rerunning the campaign or repackaging its evidence. The [study report](README.md) contains outcomes, uncertainty, and historical provenance. New intensive Robust tests follow the maintained [abbreviated triad harness](../../../triad-performance.md), [computational testing policy](../../../robust-current.md#testing-computationally-intensive-robust-changes), and [material lifecycle policy](../../../development.md#material-lifecycle).

## Hardware and arithmetic contracts

Historical dispatch was based on device compute capability, with operand storage, multiply format, accumulation, output and scale handled as separate properties. Generated instruction evidence was required before claiming a native tensor format; an API name or FP32 output did not prove which instruction executed. In particular, consumer RTX 4090/SM89 measurements did not imply laptop or eGPU performance, and native floating E2M1 FP4 was outside this hardware study. These distinctions are useful when reviewing numerical evidence, but this dated table is not a supported implementation matrix.

## Findings and limits

The study exposed two numerical implementation hazards: a generated `ldexp` path mishandled subnormal values, and SM89 conversion to FP8 could truncate through FP16 before FP8 rounding. The corrected product-scale epilogue and explicit FP8 pre-rounding passed their recorded discriminators. On identical represented FP8 inputs, K32 partial products with explicit FP32 additions reduced maximum arithmetic error 21.85×/26.62× for E4M3/E5M2 at roughly the same 0.15 ms kernel time; this was GEMM arithmetic evidence, not receiver performance.

Packing dominated a synthetic product comparison: fused packing reduced both-operand packing plus product from 1.302 ms to 0.307 ms, while strict FP32 product alone was 0.419 ms. Sustained complete workspace replays showed strict FP32 at 15.836 ms per capture and fused FP16 at 16.196 ms; copied batch timing was slower still. Thus the packing optimization did not make FP16 faster than strict FP32 for the measured complete workload.

The fresh screen included 128 sampled captures, 32 of them noise-only, and compared multiple policies. All policies had zero admissions in those 32 null captures, but the bound was far too weak for either operational rare-admission target. The −8 dB-Hz stratum recovered 32/32 signals; other strata had fewer successes. These fixed-policy, flat-channel results do not establish changing-channel performance, operational reliability, or 0.1/0.25/0.5/1 dB sensitivity budgets. No precision policy was promoted to a default. The report preserves the exact strata and pointwise uncertainty.

This study changed no ordinary Robust implementation or default. The retained archive and its hashes identify historical records; they are not build inputs, regression dependencies, or a reason to keep copied sources and regenerated campaign outputs. Preserve only evidence needed for a concrete unresolved question, then remove its temporary material when resolved.

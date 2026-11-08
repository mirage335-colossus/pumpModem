# Exhaustive FP4 correlation study protocol

This document records the scope and interpretation of a completed, optional
FP4 study for the isolated triad-v3 receiver. It did not integrate with ordinary
Robust reception or change a receiver default. The measured outcomes and
provenance are in the [study record](README.md). This is historical context,
not a recipe to repeat or extend its campaigns.

The study compared exhaustive native FP4 correlation against strict FP32 and
FP64 references on the same sampled observations. It retained the complete
candidate/epoch, lag and CFO search, coupled channel taps and downstream phase
and channel inference. Its numerical reference used sampled templates and raw
sampled observations; fixed-template approximation and data-dependent
observation quantization were treated as different statistical models. Native
instruction execution was verified separately from API acceptance. Erasure
masks were rejected by the experimental backend until lag-dependent Grams
could be implemented. These are useful interpretation constraints when reading
the historical results, not implementation requirements for a replacement.

## Frozen comparisons

The historical comparisons included NVFP4 and MXFP4 correlation, strict FP32,
FP64 correlation and inference, plus opt-in reconstruction variants. Search
geometry, candidate hypotheses and sampled channel effects were kept explicit;
small or repeated timing batches were not counted as independent physical
trials. The study also distinguished synthetic GEMM timing, correlation-pipeline
timing and complete-receiver timing. See the [results](README.md) for measured
values, device context, limitations and source/input provenance.

## Numerical and physical gates

The study considered correlation and likelihood error, search-ranking changes,
correct and incorrect admissions, and sensitivity budgets. Its measured
development samples did not calibrate operational false-admission tails or
resolve the stated sensitivity-loss budgets. In particular, zero events in a
small holdout and synthetic arithmetic speedups do not establish operational
performance. The detailed historical thresholds and assumptions are retained
with the results so they are not mistaken for current acceptance criteria.

Cheap deterministic correctness checks remain appropriate when they answer a
current question: validate complete-bank indexing and legal sampled windows,
compare small fixed inputs against the strict reference, and check numerical
invariants before measuring an abbreviated workload. New intensive Robust
measurements must follow the maintained [abbreviated triad harness](../../../triad-performance.md)
and [computational testing policy](../../../robust-current.md#testing-computationally-intensive-robust-changes).
Keep only evidence needed for a concrete current question under the
[material lifecycle policy](../../../development.md#material-lifecycle); remove
temporary inputs and artifacts when that purpose ends.

## Timing and acceptance

The historical study's complete-job throughput objective was not met, and no
precision mode was made a default. Pipeline results varied by device and
workload. For the fixed 36-hypothesis workspace on RTX5090, complete compute
time was 13.21 ms with strict FP32 and 14.01 ms with native FP4; other large
shapes did gain from FP4. The study also showed why synthetic matrix throughput,
resident timing and complete reception must remain distinct. Its rental rates,
campaign budgets, workload recipes and technical artifacts belong to the
historical record, not future routine testing. For new work, predeclare minimal
path strata, sample budget, uncertainty decision and stopping rule as described
in the maintained testing policy.

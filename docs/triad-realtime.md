# Hierarchical acquisition: retained CPU techniques

Current implementation and validation rules are in [Robust architecture](robust-current.md)
and [abbreviated testing](triad-performance.md). This note preserves research
ideas and source-bound evidence, not a second implementation or a recipe to
rerun retired campaigns. Historical results do not qualify changed code.

The earlier complete `RealtimeReceiver` is retired. Its useful proposal kernels
are part of the current [weak acquisition stage](triad-weak-streaming.md), not a
parallel receiver. The [local CPU diagnosis](validation-data/triad-v3/2026-11-05-local-cpu/README.md)
showed the cost of multiplying a joint likelihood across a full arrival/epoch/
frequency/clock product.

The cheaper approach shares observation FFTs and cached template/Gram work,
retains proposals for both bit alternatives, and refines one correlated sparse
candidate list. Midpoint-preserving clock coordinates avoid gratuitous timing
shifts during refinement. The final dynamic likelihood uses original complex
samples; a static proposal statistic does not make random fading disappear.

Valid-lag correlation can avoid padding for unused convolution tails when every
retained template index lies inside the observation. Pulse and filter tails
still belong to support checks. Small well-conditioned Hermitian quadratic
forms can use explicit arithmetic; ill-conditioned/rank-deficient cases require
an honest fallback. Higher-precision Gram/rank/reduction work may remain cheap
beside lower-precision FFT/template bulk work.

The [November 6 study](validation-data/triad-v3/2026-11-06-realtime/README.md)
recorded 16/16 target-case admissions, a 61-capture signal/null/stress set, and
18.5–21.4 seconds of fresh-process acquisition for 84.7 seconds of I/Q, with
about 8.4 seconds of additional cold template preparation reported separately.
Weak-link and renewing-ray stresses remained unresolved. These frozen results
are not current throughput, physical admission or full message qualification.

Use the current source-bound tests and [abbreviated harness](triad-performance.md),
not the deleted receiver CLI or sampled campaign. A sparse proposal procedure
needs its own admission accounting; old fixed-bank thresholds do not transfer
merely because the final likelihood kernel is shared.

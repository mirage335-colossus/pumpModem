# Search arithmetic policy

The maintained implementation/evidence is in
[search compute](search-compute.md#experimental-cpu-search-arithmetic-local-manual-candidate).
Algorithm, arithmetic and template reuse are independent choices.

| Control | Current CPU execution |
| --- | --- |
| Default — automatic | Demonstrated native FP32 paths and eligible reuse; wider fallbacks where required |
| FP32 minimum | Prohibits lower-precision operands; currently essentially the same supported paths as Default |
| FP64 force | Original unquantized templates and FP64 search; bypasses approximate template interpolation; exact reuse allowed |

Precise timestamps, epochs and phase origins are retained. Scheduled FFT tiles
can use native FP32 while original guard replay, real I/Q Gram, energy,
covariance and final scoring remain wider. See the
[moderate follow-up](scheduled-hops.md#moderate-schedule-follow-up-14-october) for
actual supported/fallback distinctions. No INT8, GPU or precision-only planner
speed discount is introduced by this stage. GPU and ARM qualification remain
outstanding. FP64 operands do not remove every algorithmic approximation.

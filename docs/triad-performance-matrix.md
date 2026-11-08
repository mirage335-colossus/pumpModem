# Abbreviated Robust coverage and historical timing lessons

The former full-workload performance guard, completion runs, calibration and
campaign matrix are retired. The [current performance policy](triad-performance.md)
describes the only maintained intensive-testing route. This page records the
coverage distinctions that remain useful when designing small checks.

| Boundary | Small useful discriminator | Claim limit |
| --- | --- | --- |
| Source and shaping | Exact geometry, chunk identity, short basis and pulse checks | No sensitivity or whole-observation power claim |
| Shared FFT proposal | Fixed-geometry preparation and score work | No complete acquisition latency claim |
| Gaussian state | Tiny update batches with declared state shape | No unknown channel/noise qualification |
| Timing forward | Bounded transition and observation work | No long-run tracking reliability claim |
| Native audio mapping | Byte/coordinate identity, limits, EOF and invalid-input guards | No physical device or channel qualification |
| Current acquisition/tracking | Representative deterministic fixtures and public-prior accounting | No exhaustive-bank or daily physical false-admission claim |

The abbreviated harness reports cold preparation separately, uses predeclared
looks and independent abbreviated holdouts, and withholds extrapolation when
its fixed-geometry cost model is unsupported. A kernel projection is not a
projection of complete receiver latency. Repeated units must retain the same
cache, state, precision, device and numerical-library regime.

Historical full-run measurements remain useful only with matching source,
configuration and input provenance. The [November 3 evidence](validation-data/triad-v3/2026-11-03-broad/README.md)
and [FFT timing study](validation-data/triad-v3/2026-11-03-fft-timing/README.md)
illustrate separate preparation/scoring paths and coverage gaps. They neither
require rerunning the old matrix nor certify this cleaned source. Preserve
failed, incomplete and unsupported outcomes when consulting them.

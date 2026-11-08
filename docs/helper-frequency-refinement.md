# Helper frequency-refinement techniques

Current implementation and validation rules are in [Robust architecture](robust-current.md)
and [abbreviated testing](triad-performance.md). This note preserves research
ideas and source-bound evidence, not a second implementation or a recipe to
rerun retired campaigns. Historical results do not qualify changed code.

Earlier matched Helper-only studies compared short sub-aperture phase slopes,
weighted whole-aperture moments, partitioned tapers and bounded local iterations.
A prediction chose a coordinate/search neighborhood; it did not supply another
independent frequency measurement. Whole-dwell and partitioned Hann weighting
had different endpoint weights, so one taper was not automatically more efficient.

The covariance must match the actual taper and correlated two-pilot observations.
Thermal covariance, oscillator phase diffusion and model-mismatch allowance are
separate contributions. A fitted conditional engineering interval is not a
calibrated confidence claim under colored interference, leakage or selection.
Whole-fit and recent-history agreement is useful, but a few small point errors
cannot establish that a mismatch guard is unnecessary.

Simultaneous pilot phase differences cancel a hop's unknown common RF phase.
They retain an unknown differential intercept and can inform audio/RF clock
models. Differential propagation phase remains confounded with clock evolution
unless independently bounded. A high-pilot-only phase can remain useful when a
dual-pilot blend is inconsistent; failure of that blend does not erase the dwell.

The original scenarios used randomized keyed hop geometries and froze results
at their stated observation times. Seeds that reused key material were not
independent schedule keys. [Helper evidence](validation-data/helper/README.md)
and [continuation lessons](helper-continuation.md) preserve limits and failure
cases; the old implementations and reproduction campaigns are removed.

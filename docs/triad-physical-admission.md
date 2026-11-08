# Historical native-ADC admission argument

Current implementation and validation rules are in [Robust architecture](robust-current.md)
and [abbreviated testing](triad-performance.md). This note preserves research
ideas and source-bound evidence, not a second implementation or a recipe to
rerun retired campaigns. Historical results do not qualify changed code.

The separate fixed-bank `PhysicalReceiver` and its profile calibration drivers
are retired. The current adaptive tracking score does not inherit their
conditional admission claim. The [short cable record](validation-data/triad-v3/2026-11-06-physical-admission/README.md)
preserves two DATA admissions, one rejected independent noise control, actual
sample rates, retained failures and uncertainty.

## Representation lesson

Scoring original real ADC samples avoids treating a changing ASRC output as
white noise. A fixed positive-frequency DFT projection excluding DC, Nyquist
and conjugate duplicates retains both real quadratures, image terms and improper
complex relationships. Removing constant DC does not remove arbitrary varying
interference. Local resampling coordinates and remote timing uncertainty remain
separate; a public fixed template bank must not use transmitter timing truth.

## Conditional argument and its limit

The research assumed a global joint sub-Gaussian bound
`E exp(tᵀn) ≤ exp(v ||t||² / 2)` for every real vector t over the complete noise
observation. For jointly Gaussian noise, covariance domination by vI suffices.
Marginal Gaussian-looking samples or a measured PSD maximum do not establish
that joint assumption across all blocks.

A fixed normalized signal-prior mixture then has null expectation at most one;
a fixed-endpoint Markov bound and union accounting can cover declared attempts.
This is not arbitrary adaptive stopping, learned-prior selection, or a bound on
wrong-bit decisions when a signal is present.

A few cable recordings cannot establish a rare daily failure guarantee for
future noise. Device/mixer/rate/source identity, clipping and continuity are
necessary context, and finite diagnostics can expose contradictions rather than
prove the global bound. Preserve that distinction in future work; do not revive
the retired profile-building and full qualification pipeline.

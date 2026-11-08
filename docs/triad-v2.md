# Historical Rate × Added Symbols experiment

Current implementation and validation rules are in [Robust architecture](robust-current.md)
and [abbreviated testing](triad-performance.md). This note preserves research
ideas and source-bound evidence, not a second implementation or a recipe to
rerun retired campaigns. Historical results do not qualify changed code.

V2 clarified the distinction between inner Rate and Added Symbols and between
each bit's own energy collection and the duration of a multi-bit frame. Complete
inner coefficients, pulse support and dwell diversity constrained the geometry;
interleaving moved whole cells. That implementation is retired. Current source
semantics belong to [v3](triad-v3.md) and [independent patterns](triad-patterns.md).

Useful accounting lessons remain: power is summed over the carrier triplet,
not assigned in full to each carrier; received power must not be renormalized
to compensate for channel loss; sample-noise variance scales with sample rate
so oversampling does not improve a physical C/N0 budget. Nominal coefficient
energy and actual shaped waveform mean power are distinct measurements.

Independent audio-clock error changes timing and the audio conversion residual;
RF oscillator error is multiplied by the RF translation frequency. Transmitter
phase acts before propagation and receiver phase afterward. A stochastic
oscillator fixture is not a measured device phase-noise spectrum.

The public arrival/epoch prior must not be recentered from simulated timing
truth. Every admitted candidate needs its declared observation support,
including delayed tails. The [October 31 v2 evidence](validation-data/triad-v2/2026-10-31/README.md)
contains physical-link examples, marginal-sensitivity and false-admission
failures. These dated results do not validate current source or receiver code.

# Historical first three-carrier experiment: retained lessons

Current implementation and validation rules are in [Robust architecture](robust-current.md)
and [abbreviated testing](triad-performance.md). This note preserves research
ideas and source-bound evidence, not a second implementation or a recipe to
rerun retired campaigns. Historical results do not qualify changed code.

The first prototype explored adjacent carriers with keyed amplitude permutations
and phase choices, filtered private patterns, cover, interleaving and dwell
windows. Its fine-symbol convention differs from later Rate × Added Symbols
semantics. Its transmitter and receivers have been removed; the current waveform
is [IndependentApskV2](triad-patterns.md).

The durable lessons are that equal triplet coefficient energy does not ensure
an equal composite envelope, and nonzero constellation points do not prevent
zeros after pulse overlap, carrier summation or propagation. Finite pulse tails
and dwell windows matter beyond ideal occupied-bandwidth formulas. Neither
noise-like appearance nor sparse private patterns prove Gaussian white statistics
or low probability of intercept.

The experiment compared amplitude, differential and segmented complex evidence.
All first projected samples onto shaped carrier cells. Differential products
formed after projection could not recover coherence already lost inside that
aperture. Coarse frequency-grid losses were not an optimized receiver limit.

The [October 31 v1 measurements](validation-data/triad/2026-10-31/README.md)
retain source-bound examples and failures. They apply to that prototype's symbol
convention and finite observation windows, not current sensitivity, admission,
GUI integration or message completion.

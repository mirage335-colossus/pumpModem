# Historical probability-model coverage lessons

Current implementation and validation rules are in [Robust architecture](robust-current.md)
and [abbreviated testing](triad-performance.md). This note preserves research
ideas and source-bound evidence, not a second implementation or a recipe to
rerun retired campaigns. Historical results do not qualify changed code.

The old single-carrier probability estimators, planner forecasts and EME
qualification routes are removed. They are not reference models for the
canonical triad receiver. Robust operation is connected to the native GUI
session, but [Link Planner](link-planner.md) receive-probability and CPU forecasts
remain unqualified for that receiver. The provisional native duration model
does not establish those forecasts or a physical-link success probability.

The reusable lesson is to bind a probability model to the statistic actually
selected: coherent correlation, segmented complex evidence and amplitude-only
statistics have different distributions and nuisance assumptions. A model of
one selected backend must not silently substitute for another. A first-bit
reference also does not describe a whole draft, continuing reception, or
physical completion.

Frequency hypotheses, timing choices and repeated attempts contribute to the
admission family. Search width and candidate correlation affect both sensitivity
and multiple-testing accounting. A conditional AWGN calculation is neither a
physical measurement nor a lifetime false-admission guarantee. An unavailable
model should be reported as unavailable, rather than filled with a convenient
number from a different detector.

Current triad [admission research](triad-noise-calibration.md) and
[weak-acquisition limits](triad-weak-streaming.md) carry forward those distinctions
without preserving the retired estimators, their calibration programs or full
qualification recipes.

# Weak-signal correlation and spectral limits

This is a retained research note, not an operating or qualification guide. The
[current Robust architecture](robust-current.md) and [abbreviated testing
policy](triad-performance.md) govern new work. The old single-carrier receiver,
planner and standalone campaign tools have been removed; their results do not
qualify the current three-carrier stages.

A known waveform can be detected below instantaneous chip noise by accumulating
correlation energy over a coherent observation. Individual chip phases need not
be separately detectable. The available gain depends on total received energy,
waveform separation, nuisance-parameter uncertainty and search multiplicity.
A narrow ideal matched-filter example does not establish acquisition probability
for the complete [current receiver](triad-v3.md).

## Phase and bandwidth

Fast small phase changes create sidebands at their modulation frequencies. For
small sinusoidal phase depth beta, first-sideband power scales with beta squared.
Reducing depth reduces energy in the sidebands; it does not move them into an
arbitrarily narrow band. Similarly, coefficient amplitudes are not the composite
three-carrier waveform after pulse shaping and channel filtering. Use actual
waveform energy and spectrum when comparing candidate representations.

Residual frequency rotates phase throughout the observation. Random phase,
sample-clock drift and multipath change the matched waveform in different ways.
Longer integration can lose coherence; independent amplitude fitting at every
sample can absorb the signal distinctions it was meant to detect. Shorter
coherent sections preserve some information but require an explicit likelihood
for relationships between sections. See [weak-link theory](weak-link-planning.md)
and [phase and amplitude research](amplitude-probability.md).

## Evidence discipline

Distinguish a fixed known waveform, a searched hypothesis bank, and repeated
operational decisions. Null calibration and receiver success must refer to the
same public inputs, nuisance model and search procedure; hidden transmitted
truth is diagnostic only. Statistical samples must follow the [hierarchical
abbreviated policy](triad-performance.md), with uncertainty and uncovered regimes
reported instead of compensated by a long brute-force run.

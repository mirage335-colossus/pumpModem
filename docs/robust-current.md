# Robust modem: current architecture and status

The canonical Robust implementation is the three-adjacent-carrier native
`IndependentApskV2` path: waveform construction, shaping, streaming sampled
channel, and Helper generation share native code. The retained
[pattern research](triad-patterns.md), [stochastic channel findings](triad-stochastic.md),
[shaping findings](triad-shaping.md), and [channel model contract](channel-models.md)
provide relevant technique summaries and measured limits.

The Python `WeakReceiver` acquisition and `PracticalTrackingReceiver` tracking
implementations are successive stages. They are not an integrated end-to-end
receiver and are not connected to the ordinary Robust GUI. The GUI controls are
retained, but Robust GUI transmission/reception remains unavailable until that
connection is implemented. Do not describe the staged Python receivers as a
qualified product path or imply GUI support.

Older single-carrier Robust and standalone triad/Helper implementations are not
supported current paths. Protocol breaks belong in a new Git tag and a separate
binary release, not a parallel in-tree implementation. If a security fix must
cover a prior release, backport it narrowly to strictly sanitized dynamic-input
surfaces.

## Testing computationally intensive Robust changes

Use a hierarchical, computationally minimal statistical method. Predeclare the
decision, covered path strata, sample allocation and stopping rule. Begin with
cheap deterministic checks, then only the smallest representative statistical
samples needed to estimate the declared behavior. Use validated abbreviated
measurements and extrapolation only when the assumptions, uncertainty and
source/configuration provenance are explicit and applicable. Add samples only
when the predeclared uncertainty rule requires them.

Do not run or retain full-observation, sustained, multi-day, exhaustive sweep,
or whole heavy-calibration campaigns as routine regression or as fallback when
an abbreviated result is inconclusive. Resolve a gap with a smaller discriminating
experiment or report the unresolved scope. Historical full-run measurements are
anchors only for a matched source, inputs, configuration, and computational
regime; they do not qualify changed code. RunPod may accelerate the same minimal
statistical method, but does not justify heavier campaigns.

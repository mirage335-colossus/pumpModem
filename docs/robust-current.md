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

For alternative receiver techniques, retain concise current-relevant findings,
including negative results, assumptions, limitations and provenance; do not keep
obsolete receiver implementations or entire historical campaigns as examples.
Keep underlying data only for a specific current question that its summary
cannot answer, and remove temporary experiments when their documented purpose
ends. The [material lifecycle](development.md#material-lifecycle) applies to
tracked files, ignored local evidence and remote storage alike.

## Transmit key policy

Selecting the public Amateur profile changes key material, not the Robust
waveform or encryption protocol. Ordinary Robust DATA keeps its configured key
and encryption setting; any enabled Helper uses the configured effective key.
At the lower-level resolver, Message mode preserves the caller's DATA encryption
flag; it does not disable keyed Helper metadata. Transmit-noise DATA always uses
fresh, independent operating-system CSPRNG keys and effective encryption, even
when Amateur is selected. Its dual-lane roots are created once per transmission
and reused for its samples; private roots are never exported or logged. If Helper
is enabled for noise transmission, its callsign uses the public Amateur key so
listeners can decode it; the Helper timecode is public. The ordinary Helper
callsign uses the configured effective key. The Helper callsign stream advances
on absolute 400 ms dwell epochs; reusing the same key and the same dwell repeats
its ciphertext.

| Transmission | Robust DATA key and encryption | Helper callsign key |
| --- | --- | --- |
| Ordinary, private profile | Configured effective key; encrypted when enabled | Configured effective key |
| Ordinary, Amateur profile | Public Amateur key; preserves the configured encryption setting and is publicly decodable when enabled | Public Amateur key; publicly decodable |
| Transmit noise, either profile | Fresh independent CSPRNG keys; encrypted | Public Amateur key; publicly decodable, if Helper is enabled |

The [background-noise generator](triad-background-noise.md) also creates its
own independent CSPRNG key, separate from DATA and Helper keys. Ordinary export
uses the resolver; transmit-noise behavior has API-level checks only. No
operational noise command or connected Robust GUI transmitter is available.

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

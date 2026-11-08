# Robust modem: current architecture and status

The canonical Robust implementation is the three-adjacent-carrier native
`IndependentApskV2` path: waveform construction, shaping, streaming sampled
channel, and Helper generation share native code. The retained
[pattern research](triad-patterns.md), [stochastic channel findings](triad-stochastic.md),
[shaping findings](triad-shaping.md), and [channel model contract](channel-models.md)
provide relevant technique summaries and measured limits.

The manual-case suite's acquisition stage uses noisy Helper estimates and
`FFTScreen` proposals; its bounded tracking subwindow uses
`PracticalTrackingReceiver`. These are sequential research stages, not an
integrated end-to-end receiver and not connected to the ordinary Robust GUI.
The GUI controls are retained, but Robust GUI transmission/reception remains
unavailable until that connection is implemented. Do not describe these staged
measurements as a qualified product path or imply GUI support.

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

For the current editable manual cases, first run `./build.sh test triad`, then
run the local CPU-only subwindow suite:

```sh
python3 tools/triad_abbreviated.py --cases \
  --output local-evidence/robust-cases.json \
  --exporter build/dev/triad_helper_export
```

For focused debugging, add `--case-index N` once per selected current row; the
whole list is still freshly parsed and validated, and the report will be marked
partial. A partial run does not cover the unselected cases.

The default case source is `_tests-RobustModem.md`; the parser rereads and
hashes it, preserves row order and duplicates, validates all 16 fields
strictly, and only reuses measurements for identical normalized configurations.
See [the case-suite method, metrics, and limits](triad-performance.md#editable-manual-case-subwindow-suite)
before interpreting output: acquisition is the noisy Helper plus `FFTScreen`
public proposals and tracking is a bounded `PracticalTrackingReceiver`
subwindow. Its conditional full-bit coarse sensitivity uses one fixed channel
seed and does not establish operational admission or full Robust reception.
Timing replicas are not independent reception trials; no whole-message or GUI
qualification is claimed. Source, case-list, or binary changes invalidate a
report. The one-thread default reflects matched local measurements, including
an FHSS case with no measured 12-thread speedup, and is
not asserted to be globally optimal or to establish a whole-machine CPU ceiling.
FHSS Helper gating follows the native plan; when the gated-pilot estimator is
unsupported, the EME configuration must retain the full CFO range. Case indexes
are current row ordinals in `_tests-RobustModem.md` and can change when that
manual list is edited; derive them from the freshly parsed list before a partial
run.

For the manual-case Groundwave Helper search, the caller opts into continuous
interval admission because the DATA stage consumes Helper frequency intervals,
not its diagnostic CFO points. For each retained onset aperture, clip and merge the proposed interval union to
the public prior, then compare its total width with the prior width using the
existing 80% fallback limit. Other callers keep discrete-bank admission. A
flat pilot, prior-boundary profile, inconsistent pilot regions, gated
or retuned Helper, no usable pilot, or an aperture's interval union covering at
least 80% of the prior must keep the full public DATA search. Successful onset
branches are then conservatively unioned without another width gate. Do not loosen these fallback rules to accommodate
a coarse bookkeeping grid.

For an eligible slow, non-hopping full-coarse template bank, a 5 Hz projected
profile is available when the public required band is at least 0.4 Hz and below
2 Hz (pass edge 2 Hz, stop edge 2.45 Hz). Revalidate the profile against the
current exact native basis on the local window using rank agreement, the 1%
projector and stored scoring-operator gates, and the remaining local fidelity
checks. This rate gives a 0.2 second coarse arrival step; it is a search grid,
not a continuous arrival-time certificate. Existing 10 Hz and 20 Hz profiles
remain selected for their narrower and wider eligible bands. The tested 3 Hz
profile failed the 1% fidelity gate and is not an available option. These
profiles approximate template storage, not the channel or observation filter.
When admitted, the suite also estimates complete coarse service as the measured
streaming FIR plus the slower bit's full nominal-SNR Helper/FFT search; it
excludes the separate `PracticalTrackingReceiver` and is not divided by the
host's CPU count. Confirmation is attempted only when its predeclared
84-receive cost forecast is at most 600 seconds per candidate; above that, the
model and pilot remain provisional and supply no empirical >90% bound.
An explicit `--paired-followup` option can run a separate 90% paired-event
check when its 44-receive cost gate also passes; the standard result remains
unresolved and the paired outcome is not message success.
Template-profile and preparation limits are in the linked method.

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

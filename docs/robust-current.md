# Robust modem: current architecture and status

The canonical Robust implementation is the three-adjacent-carrier native
`IndependentApskV2` path: waveform construction, shaping, streaming sampled
channel, and Helper generation share native code. The retained
[pattern research](triad-patterns.md), [stochastic channel findings](triad-stochastic.md),
[shaping findings](triad-shaping.md), and [channel model contract](channel-models.md)
provide relevant technique summaries and measured limits.

The headless `CompleteStreamReceiver` processes captured streams using native
public slot plans, complete fine tracking and keyed fresh banks. It decides
physical completion from two fully scored quiet slots after DATA; transport EOF
and received length do not choose its endpoint. Its codec returns filtered
text. The frozen native functional gate passed: it decoded `e` (`001`) after
three DATA slots and two scored quiet slots, while the captured-prefix EOF
negative control remained physically incomplete. This covers one bounded
configuration and functional path only; it is not an all-case capacity,
physical-channel or operational qualification. This headless receiver is not
connected to the ordinary Robust GUI. GUI controls remain visible, but Robust
GUI transmission/reception is unavailable pending reconnection.

Receiver orchestration must preserve the **full available logical CPU worker
budget** by default. Power or thermal limiting and temporary/minor benchmark
differences must not turn into a smaller portable default. Keep allocation
overrides explicit and stage dispatch sized to useful work, following the
[CPU allocation contract](development.md#receiver-cpu-allocation).

CPU fine processing fuses native render, projection and sufficient statistics
across independent candidate tiles in that same pool. Two ready segment blocks
share a dispatch wave; each channel model advances them chronologically before
global pruning. Exact bit-pair geometry and recurring native source lanes are
reused. Large CPU observations use bounded FIR tiles. Matched broad fine work
improved 1.66–1.75×, but full receiver saturation remains unproven. Preserve the
shared budget and sequential state semantics when removing remaining
bottlenecks. See the [restructuring evidence](validation.md#receiver-restructuring)
and [complete-suite results](validation.md#complete-headless-robust-and-manual-capacity).

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

The bounded regression and absolute-capacity tools make separate decisions.
When applicable local phase costs exist, use the preferred
[GPU-assisted manual-case forecast](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast).
Refresh missing CPU cost classes or changed local calibration after the native
triad checks. For a full new local calibration, run the dedicated suite.

```sh
./build.sh test triad
python3 tools/triad_manual_cases.py \
  --output local-evidence/robust-cases.json \
  --exporter build/dev/triad_helper_export \
  --workspace-mib 3840
```

The suite rereads `_tests-RobustModem.md` on every invocation. Repeatable
`--case-index N` is for a clearly marked partial debugging rerun; it does not
qualify omitted rows. `triad_abbreviated.py --cases` is a compatibility
forwarder. For method, caps and limits, see the
[manual capacity contract](robust-manual-capacity.md). Ordinary
[`triad_abbreviated.py`](../tools/triad_abbreviated.py) measures relative
regression; its synthetic complete-fine timing is not native-channel, quiet
message-completion, SNR or absolute-capacity evidence. See the
[regression policy](triad-performance.md).

The absolute CPU metric is elapsed receive service per received baseband second
for one stream: ratio 1 or higher fails the real-time criterion, while 0.1
represents a 90% service-time margin. Never divide service by core count or
infer a whole-machine ceiling from unmeasured work. Results are bound to their
source, case-list and native binary. Earlier stage evidence remains historical
and does not become a result for changed source or method. Consult the
[validation record](validation.md) for current outcomes and provenance. Use the
[manual capacity workflow](robust-manual-capacity.md#fast-local-cpu-capacity-measurement)
for the same-stream allocation pilot and default full-visible logical inference.
On the 6-core/12-thread host the pilot uses 1, 6 and 12 workers. RunPod measures
work demand for the forecast, and source-matched local phase timings determine
this computer's service costs.

In the complete receiver, only the fixed affine lane supplies operational
admission and the final bit. Adaptive and combined lanes still run and are
timed, but remain diagnostic. Fine likelihoods use explicit FP64 after a
high-SNR Gaussian FP32 attempt failed; precision does not adapt as fallback.
Quiet evidence uses complete orthogonal likelihood blocks wholly inside the
native public aperture (DATA support intersected with the search window and
bounded by neighboring latest/earliest DATA slots). All input remains in fine
likelihoods. Message completion still requires two scored quiet slots, without
using packet length or transport EOF. The fresh one-second, two-segment
256-initial-state transition probe is charged once per 0.5-second window
segment in the cost gate to conservatively cover switching and merging.

The [validation record](validation.md) separates per-row nominal and cold-state
estimates, actual busy-core measurements, GPU forecast evidence and sensitivity
confirmation. An allocation count alone does not establish a CPU throughput
ceiling.
`--skip-message-gate` is debug-only and prevents an integrated suite pass; the
former `--paired-followup` option is removed.

# Robust modem: current architecture and status

The canonical Robust implementation is the three-adjacent-carrier native
`IndependentApskV2` path: waveform construction, shaping, streaming sampled
channel, and Helper generation share native code. The retained
[pattern research](triad-patterns.md), [stochastic channel findings](triad-stochastic.md),
[shaping findings](triad-shaping.md), and [channel model contract](channel-models.md)
provide relevant technique summaries and measured limits.

**Current capacity scope is a configured public calendar and keyed ordinal.**
Acquisition of unconfigured waveform epochs, ordinals and timing remains
unresolved. Numerical service below 1 s/s for the configured receiver does not
establish capacity for that additional search. See the
[scope and evidence](validation.md#continuous-receiver-restructuring).

The headless `CompleteStreamReceiver` defaults to continuous listening on a
configured public calendar, using native slot plans, complete fine tracking
and keyed fresh banks. It keeps listening through noise and after completed
messages. Two fully scored quiet slots after DATA establish physical completion;
transport EOF and received length do not choose its endpoint. Its codec returns
filtered text. The receiver continuously decides bit or null; two qualifying
null slots confirm the boundary and reset codec state while listening continues.
There is no separate semantic new-transmitter decision in this contract.
The native functional gate checks `e` (`001`) after three DATA
slots and two scored quiet slots, plus a captured-prefix EOF negative control
that must remain physically incomplete. Its continuous variant also scores a
sixth quiet slot and remains listening after draining the completed message.
This covers one bounded configuration and functional path only; it is not an
all-case capacity, physical-channel or operational qualification. This headless receiver is not
connected to the ordinary Robust GUI. GUI controls remain visible, but Robust
GUI transmission/reception is unavailable pending reconnection.

Continuous listening keeps the global public slot/keyed ordinal while resetting
bounded per-message codec state. It retains future buffered input, bounds recent
diagnostics and unread completed messages, and rejects new input before consuming
it when that output queue is full. Call `drain_messages()` to take completed
messages, then `resume(wait=True)` to drain already accepted ready/in-flight
work; repeat as needed before retrying the unchanged rejected input.
`resume(wait=False)` polls without waiting. EOF drains accepted work but never
invents scored quiet. An over-limit or invalid episode is rejected;
two fully scored quiet slots are required before rearming, rather than decoding
a suffix.
Waveform-address acquisition beyond the configured calendar remains unqualified.
The native format does not require an ordinal reset at each message boundary;
the standalone exporter's per-invocation epoch/ordinal defaults do not impose
that rule on a continuous stream. Finite callers retain explicit
`continuous=False`; continued-listening checks cover the configured calendar only.

Every automatic Robust receiver mode uses `S = 2*H` software workers, where `H`
is visible hardware-thread capacity. This includes serial, finite and custom
paths; this host resolves twenty-four software workers over twelve hardware
threads. Explicit worker count `W` remains exact. The canonical continuous
pipeline's automatic controller cap is `min(S, W)`; an explicit owner count may
range from one through `W` without an arbitrary six-owner ceiling. Defaults admit
up to `2*H` independent controllers sharing `2*H` software permits.

Owners grow lazily for ready complete windows whose full public DSP and memory
bounds fit. Each job has at least one worker; no owners are spawned for empty
work. Exact per-job leases retain room for later input. Each process owns its
native source, bank and mutable caches. Completed out-of-order results free
their owners for more ready work. At most twice the resolved owner cap in
uncommitted records (`4*H` by default) await ordered parent-side bit/null and codec
decisions. Idle, job-free owners can
retire when a later larger public geometry needs a narrower pool. Fewer owners
or a late serial fallback retain `W`; inadmissible work is rejected rather than
clamped to `H` workers. Resolve the automatic budget once and pass explicit
shares downstream. Default operating RAM uses cgroup-aware available memory
minus bounded host headroom, not a half-memory or fixed 3840 MiB ceiling. See the
[allocation contract](development.md#receiver-cpu-allocation). `push()` accepts bounded input
and dispatches/polls work without waiting for each slot; it may wait when the
bounded backlog is full. Explicit `slot_processes=1`, finite mode and custom
fixtures retain synchronous processing.

Admission counts the parent, all resident owners, lookahead input and IPC
buffers. After the first fully loaded child handshake, prospective owner overhead
uses its maximum pre-DSP bootstrap RSS/HWM rather than the parent's captured-IQ
heap; actual live-owner and native workspace memory are still rechecked.
Supported Linux pipelines use sealed, read-only `memfd` input instead of copied
per-job payloads. One parent assembly transient remains charged before its array
is released; the parent keeps the descriptor/metadata for retry, and children
close read-only mappings before acknowledgement. Full DSP/native workspace,
raw-ring and result leases stay charged. Unsupported systems use the original
fully charged copied transport. Capability is checked lazily before pipeline
admission, not in finite, custom or serial paths. The full-size transport check
held three read-only 691,812,637-byte row-1 inputs at eight workers each. Full DSP
reservations and one assembly transient fit within 9072 MiB, admitting three
owners where copied transport admitted one. This is input/lifetime and admission
evidence, not row-1 DSP execution or throughput. The completed native copied/shared
comparison used three owners at eight workers each, matched 839 checked fields,
decoded `001` to `e`, completed after two quiet slots and continued through a
third; EOF alone still did not complete a message, and descriptors/children closed.
No elapsed gain or all-row shared-transport envelope is established. A runtime
full-window workspace failure closes the owners and latches the
full-budget serial fallback; attempted work remains charged. Other failures
stop processing rather than silently retrying another path. This pipeline's
current qualification is in the [validation record](validation.md#continuous-receiver-restructuring).
A final-source 49-window same-stream run with sealed input and automatic
allocation averaged **8.325 busy threads of 12 (69.37%)** over 51.898 seconds
of predeclared ready-backlog intervals. Its longest uninterrupted eligible
interval was 32.899 seconds at 7.921 busy threads; 21.238 seconds of owner
startup were reported separately. The capture contained one DATA bit and 48
quiet windows; its one-bit codec frame was invalid. All windows completed without
fallback or retry. The original exact cross-allocation parity gate **failed** because 17
slots had different dispatched tracking-task counts. After independently
excluding that allocation-dependent count, all other compared work, scores and
decisions matched. The full input digest matched before ingress; the
predeclared post-run input hash was not performed. This is a scoped utilization
observation, not an all-row pipeline capacity result. The automatic RAM cap was 8.748 GB versus the earlier
fixed 9.513 GB; the observations do not isolate a transport speedup.
[Final-source receipt](../local-evidence/final-backlog-cpu/result.json).

Before the `memfd` transport change, the enlarged automatic owner cap passed the
independently audited 49-window continuous-capture check, preserving exact exposed
DSP work on the first previously checked twenty-five windows and covering all
forty-nine. The
predeclared 55.337-second sustained-backlog interval averaged 9.730
busy-core equivalents against twelve hardware threads (81.09%). It reached
thirteen active services and fourteen resident owners within the twenty-four-permit
and memory limits. This establishes useful
same-stream parallelism for that capture, not full saturation, a paired speedup,
a result for the later shared transport or all-row pipeline capacity. Earlier
hardware-count-capped results keep their original scope.
On the earlier qualified
two-owner row-7 five-window capture, cold complete receive time fell
from 21.679 to 18.952 seconds (12.58%), including owner startup; average busy
cores rose from 2.909 to 3.566. A subsequent accounting correction reserved the
shared multiprocessing resource tracker, with a fresh three-window parity and
resource check. This is bounded row-7 evidence, not full twelve-thread saturation
or all-eight-row parallel capacity qualification. The
[current all-eight complete-work estimates](validation.md#complete-headless-robust-and-manual-capacity)
now cover one twenty-four-worker pool on twelve hardware threads with a fixed
9072 MiB cap. The audited assembly has conservative capacity passes for rows 3
and 5 and nominal passes for rows 2–8. Row 4's conservative point estimate is
4.11055 processing seconds per baseband second, a numeric failure; unresolved
model gates remain. Row 7 retains its interrupted report's missing canonical
final-runtime flag and separate external posttermination identity audit. This
assembly predates the input-transport change. A reviewed applicability receipt
and strict forecast consumer preserve its unchanged one-pool DSP/finite costs
without altering raw timings or source identities. The bridge accepts the exact
completed canonical reports for rows 1–6 and 8; row 7's interrupted report and the
audited union remain ineligible. See the [GPU-assisted procedure](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast).
This does not establish all-eight GPU qualification.
It does not qualify automatic multiowner pipeline ceilings. Serial and
parallel remain separate execution modes. Automatic serial fallback also
retains auxiliary-process memory and must not inherit explicit
`slot_processes=1` qualification.

A tested fresh-onset prototype was removed after it admitted an existing active
signal under the wrong ordinal-zero hypothesis. Its DATA-versus-noise threshold
did not resolve that competing waveform address. This rejects that supervisor
design; it does not mandate a semantic fresh-transmission classifier. Further
continuous bit acquisition needs appropriate epoch/ordinal/timing coverage,
incorrect-address checks and declared detection/error bounds. Staged or
Helper-assisted acquisition may reduce that work. Do not reset the keyed
ordinal at every quiet boundary or repeat the entire
long observation at every 30-second candidate start. Different native epochs
use independent keyed waveform material, so overlapping UTC spans alone do not
permit reuse of the old waveform prefix. Preserve these findings rather than
restoring the failed prototype; details are in the
[validation record](validation.md#continuous-receiver-restructuring).

Receiver orchestration must preserve **all available logical CPU hardware
threads** by default, separately reporting admitted software workers. Power or
thermal limiting and temporary/minor benchmark
differences must not turn into a smaller portable default. Keep allocation
overrides explicit and stage dispatch sized to useful work, following the
[CPU allocation contract](development.md#receiver-cpu-allocation).

CPU fine processing fuses native render, projection and sufficient statistics
across independent candidate tiles in that same pool. Two ready segment blocks
share a dispatch wave. Supported one-/two-tap FP64 Gaussian updates also run in
native candidate tiles across the available budget, sharing Gram validation
across coherence models. Each state advances chronologically; publication and
global pruning wait for the required tile results. Generic callbacks, erasures
and device paths retain their bounded reference processing. Exact bit-pair
geometry and recurring native source lanes are reused, and large observations
use bounded FIR tiles.

Mapped preparation now validates one bounded geometry bank and reuses its
extended-precision affine terms across both chronological blocks. Numerical
evaluation order and pin ownership stay unchanged. The matched fine-stage
comparison saved 8.31% on row-7 continuation and 4.14% on row-4 cold maximum-state
work with exact semantics and logical work. These are stage results, not
all-row or GPU/tensor speedups. The three-owner comparison missed its original
15% elapsed-gain gate. The user then adopted its modest gain and higher useful
CPU occupancy to preserve utilization on this and less power-limited computers.
Keep the original failed threshold distinct from this changed adoption decision.
Continue reducing serial controller preparation as well as exposing ready work;
see the
[accepted and rejected changes](validation.md#continuous-receiver-restructuring).

The CPU coarse path shares immutable observation spectra across workspace
partitions. Supported two-ray batches combine quadratic scoring and bounded
peak selection in a native call, with ambiguity fallback and canonical retained
scores preserving selection semantics. These changes target useful work within
one received stream. They do not establish full receiver saturation or allow
old timing calibration to qualify changed CPU work. See the
[continuous receiver restructuring](validation.md#continuous-receiver-restructuring)
and [earlier restructuring evidence](validation.md#receiver-restructuring).
The [interactive architecture report](receiver-parallelism.html) distinguishes
necessary state dependencies from implementation barriers. Its measured
[CPU timeline](receiver-parallelism.html#cycle) records the final-source
49-window scheduler and its predeclared backlog occupancy, separately from
startup, fill and drain. Its original exact tracking-task gate failed, with a
separate post-hoc allocation-normalized comparison. It establishes neither
full saturation nor all-row pipeline capacity. The [earlier two-owner timeline](receiver-parallelism.html#two-owner-baseline)
retains its selected fine cycle, controller waits, nested stages and worker jobs.
The [historical trace](receiver-parallelism.html#historical-cycle)
preserves the pre-change `3b423f9` execution, including its former three-job
Gaussian waves. Signal-time coordinates are separate from processing wall time;
recorded job spans do not measure OS scheduling or establish full CPU saturation.
The pipeline and stage-speedup explorers remain explicitly hypothetical; use
each view's pinned identity before applying its measured numbers.

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
  --exporter build/dev/triad_helper_export
```

This command's per-row service estimates use sequential windows at the automatic
`S = 2*H` allocation (twenty-four workers over twelve hardware threads
on this host). Its continuous native functional gate does not qualify the
automatic pipeline's capacity across the manual rows. Report the
execution mode explicitly; all-row parallel capacity remains unqualified.

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
for the same-stream allocation pilot and automatic software-worker inference.
On the 6-core/12-thread host the pilot uses 1, 6 and 12 workers; omitted
allocation flags select twenty-four workers for inference. Explicit
`--full-logical-inference` selects twelve as a diagnostic. RunPod measures
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

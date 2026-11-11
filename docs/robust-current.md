# Robust modem: current architecture and status

Robust uses one compiled C++ `IndependentApskV2` modem stack linked into the GUI
application. Native Helper, DATA, complete receiver processing and audio connect
through internal interfaces and shared buffers. There is no operating Python
receiver, receiver subprocess or receiver IPC. Python tools orchestrate native
tests/benchmarks and analyze their returned statistics; receiver DSP and state
belong to the compiled implementation. The default application requires neither
Python nor Torch. Optional compiled ATen and Vulkan backends can supply bulk
operations inside the same receiver control path. The shared GUI GPU selector
prefers a usable compiled CUDA receiver and then a compatible physical Vulkan
receiver; it never reports native CPU execution as GPU work. CUDA covers coarse
and eligible fine bulk stages, while Vulkan accelerates GPU search and retains
native fine processing. Neither optional path places Python in the operating
DSP chain.

New modem and intensive numerical work must follow the
[native processing and integration contract](development.md#native-processing-and-integration).
Extend these in-process interfaces and compiled CPU/GPU backends; keep Python
outside the operating DSP chain and persistent receiver state.

Automatic CPU/GPU algorithm and arithmetic selection follows the
[computational-ceiling contract](gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling):
prioritize challenging-case complete-receiver capacity and prevention of input
backlog over easy-case efficiency or unnecessary precision. Current routes and
their qualification limits below remain distinct from that design objective.

The shared GUI controller connects this stack to simulation, live audio and
presentation. Fast remains the launch default and retains its independent
controller. Implementation availability is distinct from validation: see
[the native integration record](validation.md#native-gui-integration) for completed
checks and remaining gates, and the [GUI contract](gui-contract.md) for control
mapping. No all-configuration sensitivity, real-time capacity or physical-device
qualification follows from these connections.

## Native interfaces and ownership

| Interface | Responsibility |
| --- | --- |
| `triad_stream.hpp` | Public profile and slot geometry, bounded DATA/Helper/noise emission, and the shared source used by GUI and exporter. |
| `triad_receiver.hpp` | Complete slot scoring: noisy Helper proposals, full public FFT screening, persistent fine tracking, bit/null evidence and resource preflight. |
| `triad_stream_receiver.hpp` | Bounded continuous ingress, in-process slot ownership, ordered commit, backpressure and cancellation. |
| `src/triad_receiver_executor.hpp` | Internal shared bounded worker pool for slot roots and coarse/fine stage tasks. |
| `triad_message_receiver.hpp` | Bounded native short/long text profiles, lazy interval emission, configured FEC and two-scored-quiet completion. |
| `robust_profile.hpp`, `live.hpp` | Validated GUI settings, duration/profile preparation, simulation/live session and bounded presentation snapshots. |
| `audio.hpp`, `robust_audio_frontend.hpp` | Native device ownership, routing, gain, timestamped capture and bounded real-PCM-to-I/Q filtering. |
| `triad_acceleration.hpp` | Optional compiled ATen/Vulkan leaves under the production receiver's control, state and resource contracts. |

The GUI links `datapump_robust_session`, `datapump_triad_receiver` and
`datapump_triad` statically. Source generation, Helper acquisition, coarse
screening and fine scoring use the canonical native components. Independent
slot owners keep mutable scorer state private while sharing immutable window
samples. Ordered commit preserves physical chronology even when work finishes
out of order. One lazy receiver executor shares at most `W = 2*H` software workers
across slot roots and their coarse/fine tasks for effective hardware availability
`H`; explicit allocations remain exact. A root participates in its own stage
batch; available lanes prioritize ready leaves for the oldest eligible public
slot, with queued roots using otherwise unneeded lanes.
Per-owner lane/scratch limits are fixed DSP bounds, not worker permits held
exclusively through a slot. Admission quotes the largest ready prefix at full
`W` scratch per owner, preferring balanced scratch only when it admits a
strictly wider ready prefix within the full memory bound. Each admitted
shape remains fixed through its slot; the global `W` ceiling stays unchanged.

Full per-owner DSP, input, prospective and started worker stacks, application
baseline and retained-result memory are charged. Started idle pool stacks remain
charged until close. Initialized input is covered once by footprint and ownership
floor; outstanding DSP/stacks/output bounds remain reserved because RSS cannot
attribute anonymous memory to a scorer. `peak_running_owners` counts roots in
actual scorer scope, while `peak_executor_workers` counts started pool threads;
neither is a hardware-utilization measurement. Standalone phase probes retain
their local executor. The current point improves first-message latency while
remaining slower overall than the original baseline; no throughput/utilization
gain is claimed.
See [CPU and memory ownership](development.md#receiver-cpu-allocation).

The operating CPU receiver defaults to qualified reduced-precision frequency
search after native DATA channel isolation on AVX2. Native single-ray exact
projection can use Q15 spectral products with FP32 FFTs under checked native
bounds; other eligible frequency banks use FP32 products and FFTs. For a
qualifying no-Helper, single-ray, single-arrival search, automatic mode can
instead choose FP64 chirp-Z when its estimated transform-row work is at least
four times smaller; a single frequency can instead use a direct FP64 dot under
the same modeled-advantage requirement. A single-ray bank has Gram ratio 1 and
can convert each checked template directly, with a phase-aware
admission bound charging FP64 projection and conversion overlap. Other automatic
banks remain subject to the complete bank's double-precision conditioning check. Lag/tight-clock and
ill-conditioned or unqualified CPU routes retain FP64 with explicit reasons;
coordinates, scoring and fine state remain double precision. The
[shared arithmetic policy](search-arithmetic.md) defines the automatic and
forced-FP64 controls, scaling and receipt identity. Both choices preserve public
coverage, `W=2*H` and full memory accounting. The smaller coarse bound can reduce
the slot lease when coarse is dominant; it does not imply a reduction where
Helper or fine work dominates.

The optional [Vulkan backend](triad-performance.md#local-radeon-vulkan-processing)
starts after native DATA channel isolation and accelerates GPU frequency-search
work; fine processing remains native CPU work. On the specifically qualified
local Radeon device/geometry, a scaled packed-FP16 direct correlation uses FP32
accumulation and FP64 scoring. Other cases can use FP32 direct arithmetic,
qualified FP16 FFT storage with FP32 arithmetic, an FP32 FFT or explicit native
fallback. Actual precision and fallback reason are reported. Helper, native
FIR, template generation, lag groups and the compact tight-clock FP64 class also
remain native. Stage capability/routing is explicit. An APU's GPU allocations
use shared host RAM; the bounded device workspace, staging and persistent
VkFFT program cache are charged through their ownership lifetimes.

The shared GUI's **GPU** checkbox selects the generic native receiver factory
for live reception and simulation. If compiled and usable, CUDA is preferred
and executes GPU coarse/fine bulk work; otherwise the selector tries a
compatible physical Vulkan backend, which executes GPU search and native fine
processing. The CUDA automatic policy routes qualified small public fine blocks
to native fine processing while coarse work remains on CUDA; the GUI reports the
count and fallback reason separately from precision. Its scoped geometry and
[bounded measurements](validation.md#nvidia-and-intel-gpu-toggle-integration)
preserve the slower long-case results. CUDA uses a process/device-shared bounded workspace
admitted against available memory with runtime headroom; waiting for memory is
cancellable. Automatic Vulkan GUI admission discovers a shared workspace budget
from available physical memory after runtime headroom and leases a whole coarse
session. The GUI reports current/peak aggregate leases, peak single-owner lease,
waiters and native workspace fallbacks. A geometry that cannot fit the smallest
admissible Vulkan shape uses the complete native coarse path and is reported as
`native-vulkan-workspace`; it is not counted as GPU work. Explicit pinned Vulkan
probes retain their configured per-owner limit. Default may choose the scoped
native chirp-Z crossover on qualified Vulkan geometry;
FP32 minimum disables it. CUDA's current automatic FFT work is FP32, so its
FP32-minimum route keeps the same FP32 bulk arithmetic and higher-precision
sensitive stages. FP64 force cannot be combined with GPU. Readiness, backend
identity, actual work, selection reason and fallback are reported; a CPU route
is never counted as GPU work. Device initialization stays on the session
manager thread, and bounded backend state is released on reconfiguration/close.
The full `W=2*H` software worker budget is preserved. See the [shared control
contract](gui-contract.md). These implementation details do not establish
physical-device performance or qualification; see the [current device
evidence](validation.md#local-radeon-vulkan-backend).

The receiver control API accepts bounded chunks with explicit sample addresses
and optional validity masks. Backpressure rejects input without consuming it;
the caller waits for one completion when needed, drains available messages and
retries the same chunk without forcing a full receiver drain. EOF still drains
all accepted work. Missing samples are erasures within declared bounds, never
invented quiet evidence. The control API runs outside the real-time audio
callback. Profile preparation, device work and receiver work have cancellable
owners; GUI polling does not wait for a full receiver window.

## Configured-calendar reception

Both peers must agree on the public calendar epoch, initial keyed ordinal,
waveform profile, key and FEC mode. The GUI exposes calendar epoch and ordinal
in Robust setup. Live reception also requires a declared local input noise
floor in dBFS/Hz; link-budget noise in dBm/Hz is a different quantity. Capture
uses actual provider timestamp information and its uncertainty. A software
callback timestamp is not a substitute for sample timing.

The public calendar advances through DATA, quiet and scheduled waits; message
completion or cancellation does not reset the waveform ordinal. Two fully
scored quiet slots after DATA establish physical completion. Interior missing
slots remain erasures if transmission resumes. Transport EOF drains accepted
work without inventing quiet slots. Invalid or over-limit episodes require two
scored quiet slots before rearming, preventing suffix decoding.

Unknown waveform epoch, ordinal and start-time acquisition remains outside this
scope. A removed fresh-onset prototype admitted an active signal under a wrong
ordinal-zero hypothesis: DATA-versus-noise evidence did not distinguish the
competing address. Retain that negative result. Further acquisition needs
appropriate address hypotheses, incorrect-address checks and measured costs;
no semantic transmitter-identity classifier is required. Different keyed epochs
cannot reuse waveform prefixes solely because their UTC spans overlap.

Fine processing runs fixed-affine, adaptive and combined evidence. Only the
fixed-affine lane supplies operational admission and the final bit; the other
lanes are diagnostics. Quiet evidence uses complete orthogonal blocks wholly
inside the native public aperture, bounded by neighboring possible DATA support.
All input still participates in fine likelihoods. No received length or codec
field controls DSP geometry, allocation or physical completion.

## GUI settings and operating limits

The shared profile maps encryption, Helper power/ratio/callsign, DATA transmit
power, path loss, clock policy, oscillator models, spreading, burst waits, FHSS
and channel controls to the native components. Rate is the native inner rate;
Added Symbols and Interleave select native lanes and spreading. Invalid audio
bandwidth or fixed Helper placement fails configuration explicitly. None/Off
channel choices preserve an explicit unperturbed baseline.

Target SNR is one shared C/N0 value, default 32 dB-Hz. It drives provisional
native duration planning, separately from the simulated link's actual
transmit-power/path-loss/noise-density calculation. Planning uses a fixed
public message assumption rather than the draft length. A predicted duration
is not a sensitivity guarantee or a real-time capacity result. FHSS profiles
control the canonical envelope and illustrative schedule; they do not retune
external radio hardware. See [duration planning](triad-duration-planning.md),
[burst scheduling](symbol-burst.md) and [channel models](channel-models.md).

Text up to 16 bytes retains the native short dictionary profile. Longer text
uses periodic 192-bit markers and 128-byte fixed codewords with configured Off,
RS20 or RS60 coding, optional raw LZMA2 compression and a private-key interval
tag when keyed. The GUI byte limit is 1 MiB; its escaped text representation has
a separate 4 MiB limit. Native profile quotas are local resource settings.
Transmit preparation and codewords are generated off the GUI thread; lazy
emission avoids a whole-message bit vector. Exact 1–208-bit raw drafts still
bypass text coding and FEC.

Physical completion remains two scored quiet slots. No received length selects
DSP geometry, memory bounds or a message endpoint. The marker cadence does not
reset the global waveform ordinal. The keyed interval domain is new; it does
not promise compatibility with retired long-message formats. Valid short
prefixes can overlap an incomplete marker, and missing whole final uncompressed
intervals can look like a shorter valid stream. Interval authentication is not
whole-message endpoint authentication. See [protocol](protocol.md).

Waveform, waterfall, receiver I/Q and pattern evidence/steps consume bounded
session diagnostics. Receive evidence comes from actual received observations
and complete native score records. Selected simulation parameters are not
receiver estimates. Audio routing, duplex mode, TX volume and exclusive mode
use the existing native audio interface; physical device behavior requires its
own qualification.

## Historical evidence and reference lifecycle

Historical Python-orchestrated complete-stream and process-scheduler results
remain explicitly scoped in [validation](validation.md#continuous-receiver-restructuring).
They explain useful algorithm choices and previous margins, not native GUI
performance. The former process-owner, bootstrap and `memfd` transport contracts
are superseded by native in-process ownership. The unchanged `2*H` allocation,
full resource accounting and physical-completion requirements remain in force.

Retained useful findings include shared observation spectra and bounded native
coarse peak selection, cached affine template geometry, native candidate-tile
projection and Gaussian updates, and chronological persistent state. Historical
mapped preparation saved 8.31% on row-7 continuation and 4.14% on row-4 cold
maximum-state work. These are source-bound stage observations, not new native
speedups. The three-owner comparison missed its 15% throughput gate; the user's
later acceptance of its smaller productive gain does not change that failure.
The [architecture timeline](receiver-parallelism.html) depicts the measured
historical runtime and must not be read as the current application's scheduler.

Python DSP implementations, their implementation-only tests and copied
experiment source are retired. Current tools call
the production C++ stack; Python is limited to orchestration and statistical
analysis. Preserve concise historical choices, failures and provenance, not a
second operating implementation. Historical Python timings are inadmissible as
native CPU calibration; changed native work needs fresh affected timings and
independent holdouts. Keep raw evidence only for a concrete unresolved comparison
or required validation claim, with cleanup when that purpose ends. Follow
[material lifecycle](development.md#material-lifecycle). Older protocols belong
in Git tags and separately versioned binaries.

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
own independent CSPRNG key, separate from DATA and Helper keys. Ordinary and
noise transmission use the same native resolver in the exporter and GUI session.
Helper-only transmission emits Helper without DATA. Current coverage is in the
[validation record](validation.md#native-gui-integration).

## Testing computationally intensive Robust changes

Start with cheap deterministic native component, shared GUI and audio checks:

```sh
./build.sh test triad
./build.sh test gui
```

Run other affected named groups from the [build guide](building.md#testing-stages).
These checks establish their stated functional coverage, not capacity or
communication success probability. The new native runtime needs applicable
same-stream phase timings before a CPU forecast can qualify it. Old Python
orchestration timings and process-bootstrap costs cannot simply be relabeled
as native calibration.

`tools/triad_manual_cases.py --probe build/dev/triad_receiver_probe` measures the
current compiled receiver through bounded native receipts. It reads all sixteen
strict fields in `_tests-RobustModem.md`, preserves ordered duplicates and can
select explicit partial rows with repeatable `--case-index N`. Reuse of identical
normalized inputs requires `--reuse-identical`. `tools/triad_abbreviated.py`
compares native computational regression against a matched native baseline.
Python performs orchestration and statistical analysis only. Neither old Python
reports nor transport applicability exceptions supply current calibration. Follow
the [manual capacity contract](robust-manual-capacity.md) and
[GPU-assisted procedure](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast).

Computationally intensive checks must predeclare path strata, minimal sample
budget, uncertainty decision and stopping rule. Run native triad checks before
refreshing local calibration. Use bounded same-stream measurements and fresh
holdouts for affected CPU phases; GPU timing or a hypothetical optimization
is not CPU service. Full, sustained, multi-day, exhaustive and whole heavy
calibration campaigns are not routine tests or fallbacks.

Complete service includes Helper acquisition, full proposals, fine search,
tracking, unsuccessful candidates, quiet completion, reacquisition, recurring
preparation and orchestration. Report nominal and conservative service,
uncertainty, margin and unresolved work for each current row. One processing
second per received baseband second or more is a numeric real-time failure;
do not divide service by core count. Missing work prevents a complete capacity
pass. `--skip-message-gate` is debug-only and prevents an integrated suite pass.
Changed source, runtime, arithmetic or allocation needs explicit applicability
and affected measurements. [Validation](validation.md) keeps functional parity,
sensitivity, forecasting and real-time capacity decisions separate.

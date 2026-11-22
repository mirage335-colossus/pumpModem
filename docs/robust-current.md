# Robust modem: current architecture and status

For practical setup and situation-dependent choices, start with the
[operating guide and experimental lessons](indexes/robust.md). This page defines
the current implementation; linked historical findings retain their original scope.

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
native fine processing. On an NVIDIA Blackwell device with the OpenCL module,
the Vulkan receiver scores eligible searches with the
[dense FP4 search](triad-gpu-optimization.md#dense-fp4-coarse-search-and-device-template-bank)
and computes the qualified DATA statistics with resident FP4 projection, from
[dense tiles it renders itself and a window of dispatches per call](triad-gpu-optimization.md#device-rendered-fine-tiles-and-resident-windows).
On that SM 12.0 class it also runs the segmented timing search within its
envelope and the signed-lag gain ranks on the device
([routes](triad-gpu-optimization.md#what-automatic-selection-runs)).
Neither optional path places Python in the operating DSP chain.

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

The GUI's **Wrong Key Admission** dropdown is a reversible browser filter.
Reception and decoding always retain the `current` baseline. Compact counters
record how many baseline DATA bits pass each nested scalar filter, including
bits beyond the bounded diagnostic prefix. No raw audio, extra search, codec
replay or per-message key copy is needed. Filter changes do not restart the
receiver, alter original content, or change physical completion.

For a completed rated record, the browser requires at least half its admitted
DATA bits to pass the chosen filter; ties remain visible. The pass count is
shown separately from physical erasures. Pending, non-Robust and unrated
records remain visible. This is an explicit visibility rule, not a whole-message
confidence decision. Unfiltered restores filtered records while they remain in
the bounded RAM history. The standalone native receiver/probe still supports
applying the same scalar policy directly as DATA-to-erasure admission; that
headless option is distinct from the GUI's preservation policy.

The score check uses only already computed operational bit scores, adding
constant-time scalar work and no search, DSP workspace or worker. It does not
change the waveform, calendar, duration or public geometry. This is a heuristic
against near-balanced bit scores, not a calibrated wrong-key rate or sender
authentication; one dominant wrong-key bit may pass, and wanted signals,
including strong long-window signals, may be hidden. See the
[research limits and evidence](triad-stochastic.md#wrong-key-admission) and
[GUI settings](gui-contract.md#modems-and-current-availability).

## Native interfaces and ownership

| Interface | Responsibility |
| --- | --- |
| `triad_stream.hpp` | Public profile and slot geometry, bounded DATA/Helper/noise emission, and the shared source used by GUI and exporter. |
| `triad_receiver.hpp` | Complete slot scoring: noisy Helper proposals, full public FFT screening, persistent fine tracking, bit/null evidence and resource preflight. |
| `triad_stream_receiver.hpp` | Bounded continuous ingress, in-process slot ownership, ordered commit, backpressure and cancellation. |
| `src/triad_receiver_executor.hpp` | Internal shared bounded worker pool for slot roots and their overlapped Helper, coarse and fine stages. |
| `triad_message_receiver.hpp` | Bounded native short/long text profiles, lazy interval emission, configured FEC and two-scored-quiet completion. |
| `robust_profile.hpp`, `live.hpp` | Validated GUI settings, duration/profile preparation, simulation/live session and bounded presentation snapshots. |
| `audio.hpp`, `robust_audio_frontend.hpp` | Native device ownership, routing, gain, timestamped capture and bounded real-PCM-to-I/Q filtering. |
| `triad_acceleration.hpp` | Optional compiled ATen/Vulkan leaves under the production receiver's control, state and resource contracts. |

The receiver publishes ordinary DATA, QUIET, or AMBIGUOUS decisions for each
calendar slot. Transmitting repeated values advances the keyed ordinal and
creates separate ordinary decisions; there is no special group event or cross-attempt
soft combiner. The dedicated repeated-bit GUI and receiver path tested in
November 2026 was retired. Its executed recovery remains a historical result
for that explicitly configured feature, not a qualification of ordinary
repeated transmissions. See the [retirement note](validation.md#dedicated-repeated-bit-feature-retirement--17-november-2026).

The GUI links `datapump_robust_session`, `datapump_triad_receiver` and
`datapump_triad` statically. Source generation, Helper acquisition, coarse
screening and fine scoring use the canonical native components. Independent
slot owners keep mutable scorer state private while sharing immutable window
samples. Ordered commit preserves physical chronology even when work finishes
out of order. One lazy receiver executor shares at most `W = 2*H` software workers
across slot roots and their Helper, coarse and fine tasks for effective hardware availability
`H`; explicit allocations remain exact. A root is lane zero of its own stage;
available lanes prioritize ready leaves for the oldest eligible public slot,
with queued roots using otherwise unneeded lanes. Within a slot, serial steps
[overlap independent parallel work](development.md#overlap-within-a-slot):
Helper acquisition's pilot fits, refinements and branch filters as waves; the
observation filter beside the template bank; and the fine fixed-affine lane,
the next dispatch's observation and later source tiles beside the
chronological adaptive lane in explicit fine diagnostic mode. Ordinary
operational fine scoring uses the fixed-affine `FineLanes` branch and skips
adaptive per-block prediction and pruning; separately configured varying,
drift and coherent-gain alternatives remain independent branches. A slot whose
lease has room also acquires beside
coarse preparation, which then plans its frequency grid afterwards.
Per-owner lane/scratch limits are fixed DSP bounds, not worker permits held
exclusively through a slot. For an incoming window, admission now quotes its
full-`W` shape and only the extra payload for positions vacant in the ready
prefix; it does not count a prospective owner when `O=1` or the owner cap is
full. Live RSS admission still applies, so this quote does not guarantee
admission after arbitrary process growth. Each admitted shape remains fixed
through its slot; the global `W` ceiling stays unchanged. Helper overlap with
coarse preparation is available only where the profile and resource shape
permit it, and does not guarantee lower whole-slot time under contention.

Full per-owner DSP, input, prospective and started worker stacks, application
baseline and retained-result memory are charged. Started idle pool stacks remain
charged until close. Initialized input is covered once by footprint and ownership
floor; outstanding DSP/stacks/output bounds remain reserved because RSS cannot
attribute anonymous memory to a scorer. `peak_running_owners` counts roots in
actual scorer scope, while `peak_executor_workers` counts started pool threads;
neither is a hardware-utilization measurement. A standalone slot, coarse
session or fine session owns a private instance of the same executor. The matched row-5 replay
is about 30% shorter than before the overlap and transform-order changes, with
identical scores; its [scope and limits](validation.md#receiver-overlap-and-transform-order-follow-through)
do not make it a capacity qualification.
See [CPU and memory ownership](development.md#receiver-cpu-allocation).

The operating CPU receiver defaults to qualified reduced-precision frequency
search after native DATA channel isolation on AVX2. Native single-ray exact
projection can use Q15 spectral products with FP32 FFTs under checked native
bounds; other eligible frequency banks use FP32 products and FFTs. For a
qualifying exact-source search with Helper Off, automatic CPU mode can choose
FP64 chirp-Z when its estimated transform-row work is at least four times
smaller. A candidate with multiple rays or arrivals must fit the existing
full frequency-route workspace quote. For a single ray and arrival, a single
frequency can use a direct FP64 dot under the same modeled-advantage
requirement. A single-ray bank has Gram ratio 1 and
can convert each checked template directly, with a phase-aware
admission bound charging FP64 projection and conversion overlap. Other automatic
banks remain subject to the complete bank's double-precision conditioning check. Lag/tight-clock and
ill-conditioned or unqualified CPU routes retain FP64 with explicit reasons;
coordinates, scoring and fine state remain double precision. The
[shared arithmetic policy](search-arithmetic.md) defines the automatic and
forced-FP64 controls, scaling and receipt identity. The native radix-two
transform finishes cache-sized blocks before whole-array stages, omits
butterflies whose outputs a caller declares unread, and correlates spectra
stored in transform order. Each computed value keeps the stage-major
transform's operands, so every arithmetic class keeps its exact results. Both choices preserve public
coverage, `W=2*H` and full memory accounting. The smaller coarse bound can reduce
the slot lease when coarse is dominant; it does not imply a reduction where
fine work dominates. The Helper bound follows the public calendar: its largest
transform step and a 16 MiB allowance for interval lists and records. It no
longer sets the lease of a Helper slot.

Coarse planning counts the full frequency grid from its interval endpoints
without allocating a value for each frequency. The receiver admits the full
workspace before materializing any frequency values or retaining peaks.
Checked index, transform and workspace bounds govern grids beyond the former
65,536-point limit. Helper still charges the complete fallback prior. See the
[long-duration checks](validation.md#ordinary-reception-with-conservative-settings--17-november-2026).

The optional [Vulkan backend](triad-performance.md#local-radeon-vulkan-processing)
starts after native DATA channel isolation and accelerates GPU frequency-search
work; without the Blackwell OpenCL module, fine processing remains native CPU
work. On the specifically qualified
local Radeon device/geometry, a scaled packed-FP16 direct correlation uses FP32
accumulation and FP64 scoring. Other cases can use FP32 direct arithmetic,
qualified FP16 FFT storage with FP32 arithmetic, an FP32 FFT or explicit native
fallback. Actual precision and fallback reason are reported. Helper, native
FIR, lag groups and the compact tight-clock FP64 class also remain native. So
does template generation, except for the device template bank of the Blackwell
dense search. Stage capability/routing is explicit. An APU's GPU allocations
use shared host RAM; the bounded device workspace, staging and persistent
VkFFT program cache are charged through their ownership lifetimes.

The shared GUI's **GPU** checkbox selects the generic native receiver factory
for live reception and simulation. If compiled and usable, CUDA is preferred
and executes GPU coarse/fine bulk work; otherwise the selector tries a
compatible physical Vulkan backend, which executes GPU search and native fine
processing, or the Blackwell OpenCL FP4 search and fine statistics where that
module is present. The CUDA automatic policy routes qualified small public fine blocks
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
native chirp-Z crossover on qualified Vulkan geometry; FP32 minimum disables it.
Automatic CUDA can use guarded half-cuFFT on SM89/SM120 for qualifying broad
multi-arrival searches, with FP32 FFT fallback before dispatch when its shape,
conditioning or workspace guards fail. Its FP32-minimum route instead retains
FP32 bulk arithmetic. Sensitive scoring/state remains higher precision in both
cases. Half-cuFFT is distinct from the optional Tensor Core lag-matrix path;
see the [current selection rules](gpu-precision-controls.md#gui-automatic-gpu-selection).
FP64 force cannot be combined with GPU. Readiness, backend
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

The live noise declaration is a receiver noise-model reference, not merely an
average measured power. At fixed Target SNR it scales noise variance and the
reference DATA amplitude together; actual received strength need not match
that reference. Correlated noise can require a higher spectral/covariance
envelope than its mean power suggests. The bounded atmospheric-noise replay
in the [persistent-channel follow-through](validation.md#persistent-channel-and-calendar-age-follow-through--17-november-2026)
recovers physical completion with the existing control's coupled scaling on
unchanged IQ. Its fixture-derived adjustment is not a universal setting or an
automatic calibration method, and does not validate Gaussian tail guarantees
for arbitrary impulsive noise. Keep device, gain and bandwidth calibration
separate from a propagation sensitivity claim.

The public calendar advances through DATA, quiet and scheduled waits; message
completion or cancellation does not reset the waveform ordinal. Two fully
scored quiet slots after DATA establish physical completion. Interior missing
slots remain erasures if transmission resumes. Transport EOF drains accepted
work without inventing quiet slots. Invalid or over-limit episodes require two
scored quiet slots before rearming, preventing suffix decoding.
Raw slot decisions continue during this boundary recovery. An abandoned pending
browser row is marked rejected, including when recovery and a new episode occur
between deliveries; it does not remain labelled as actively receiving. See the
[persistent-reception audit](validation.md#persistent-reception-investigation--17-november-2026).

The optional `finite_duplex` session setting is a one-message planning seam for
a trusted, bounded audio provider, currently used by the experimental RunPod
lab. It requires live full duplex, a configured public calendar and declared
input noise density. Before exposing transmit samples, the receiver must commit
two consecutive scored quiet slots and leave recovery. If later committed
evidence revokes that quiet state while the provider is preparing or accepting
the plan, transmission fails rather than using stale evidence.

The locally prepared transmission determines an exclusive capture endpoint
covering the observation of the second post-transmission quiet slot and the
input filter halo. The provider must accept that exact logical-frame endpoint
within its own explicit bounds before transmit scheduling. The endpoint limits
local capture; it is not evidence of message completion. Receiver decisions
still require two scored physical quiet slots after DATA, and early EOF,
cancellation and missing samples cannot become quiet evidence. Received packet
lengths do not select the endpoint or receiver geometry. The ordinary session
keeps its existing behavior when finite duplex is off.

A buffered provider may opt into blocking its PCM callback only if it owns and
bounds its pending input, charges that workspace, preserves dated ordered
samples and honors cancellation. The receiver keeps its bounded queue and
applies backpressure to such a provider; a physical-device callback without
this guarantee instead reports an overrun. The lab's remote audio integration
and actual physical-device behavior remain unqualified; see its
[implementation status](../_zLab/README.md).

Unknown waveform epoch, ordinal and start-time acquisition remains outside this
scope. A removed fresh-onset prototype admitted an active signal under a wrong
ordinal-zero hypothesis: DATA-versus-noise evidence did not distinguish the
competing address. Retain that negative result. Further acquisition needs
appropriate address hypotheses, incorrect-address checks and measured costs;
no semantic transmitter-identity classifier is required. Different keyed epochs
cannot reuse waveform prefixes solely because their UTC spans overlap.

Factory-created slot plans carry an admission context derived solely from that
fixed public calendar. It bounds all planned ready timestamps per input day,
including quiet/missed ordinals and clock/sample rounding; final presence uses
`log(N/1e-4)` instead of charging 216,001 impossible short opportunities.
Bare plans without the context keep the reference threshold. Both standalone
scoring of a factory-created plan and continuous reception use its context;
codec resets leave the ordinal sequence intact. The bound is conditional on
the noise model and scoped to one fixed calendar, not all keys/configurations
in an application. Original likelihood fallback order and duration planning
remain conservative; newly eligible scores still pass native line handling,
bit separation and validity checks. Wrong-key filter margins keep their
reference threshold. See [equations and scope](triad-stochastic.md#wrong-key-admission)
and [validation](validation.md#public-calendar-admission-investigation--17-november-2026).

With changing-channel controls Off, the fixed-affine lane supplies operational
admission and the final bit. The older adaptive timing and combined lanes remain
diagnostics. For a static channel whose quarter-second DATA
projection retains only DC, the fine receiver can use a whole fine cell to
preserve within-cell structure. It does so only within the mapped-basis row
limit and when the physical quiet aperture spans at least three cells. The
projection can distinguish keyed waveforms that fit almost equally well after
collapse to one scalar per block. Workspace admission uses the selected
calendar. With sufficiently separated, ungated Helper tones, the native
projection tapers its edges to suppress leakage. Eligible whole-cell blocks
use up to nine orthonormal tapered modes. The orthonormal projection preserves
the noise degrees of freedom used to score physical quiet.

Selected atmospheric or selective perturbations add bounded,
properly weighted likelihood alternatives: shorter gain-coherence models, a
chronological common-frequency mixture, medium-duration coherent Gaussian-gain
models (independent and causal AR), and coherent linear-drift matching.
The original coherent search and its public hypotheses remain available.
For public variable-channel settings where two fine cells span at least one
second, the fixed-affine lane also includes two slow complex-Gaussian gain
alternatives when a positive excursion/diffusion scale is declared. The
existing 0.5 s, 2 s and infinite-coherence models retain 0.75 total prior mass;
the two slow scales share the remaining 0.25. Their coherence times are
`tau` and `4*tau`, where `tau=max(two_fine_cells, 1/lambda)` and `lambda` is
`2*pi` times the largest declared frequency scale plus half the squared
phase-diffusion coefficient. A large upper variation allowance does not remove
the slower models: actual conditions may be calmer inside that bound. This is a bounded model family,
not a channel estimate: it uses the existing public coordinates and projected
correlations, adds two constant-size posteriors per fixed-affine coordinate,
and does not enumerate frequency histories. The public score receipt reports
the admitted coherence times; zero values mean the slow family was not
supported. See the [channel-model limits](triad-stochastic.md#nonstationary-channel-research-reconciliation)
before interpreting these scales physically.
At faster fine cadence, changing-channel operation also admits a simultaneous
stationary-plus-fading gain on the direct spatial template. Two alternatives use the
existing 0.5/2-second OU timescales and equal stationary/dynamic prior variances;
they share 0.25 prior mass while the original family retains 0.75. There is no
bit-duration cutoff. They reuse the existing template correlations and Gram,
adding only bounded posterior state and updates. Off and the original slow VLF
family remain unchanged. When the public bounds also admit a second path, the
original two-path family keeps its 0.75 share; the additive direct-only subset
uses the existing direct correlation and Gram diagonal after that calculation.
It does not fit simultaneous stationary and fading gains on both paths or
add templates, timing hypotheses or historical searches. Rank-zero and erased
blocks supply no observational evidence. The receipt distinguishes
`stationary_plus_ou` from the original or slow whole-gain family. See the
[wide-bound repair](validation.md#persistent-reception-investigation--17-november-2026)
and earlier [physical-channel investigation](validation.md#weak-physical-channel-alarm-investigation--16-november-2026)
for fixed-history gains, original failures and remaining coverage limits.

Ordinary two-tap Gaussian state stores a covariance factor in the existing four
complex slots, preserving a thin observable direction across updates. Fixed,
adaptive, slow and medium-tracking callers use that representation; process
noise and adaptive covariance merging use bounded QR operations. Known one-mode
projection geometry also prevents rounded evidence in an unobservable direction.
This adds no spatial, frequency, timing or historical hypotheses. The one-tap
and additive arithmetic remains distinct, and no hardware-capacity or all-range
numerical qualification follows from the factor representation.

The default receiver also evaluates an admitted short-coherence likelihood
when the fixed-affine score already says DATA, then makes the bit decision from
the weighted combined evidence. This spends bounded additional work to retain
changing-channel coverage under a strong coherent observation. When Helper is
Off and coarse search has no segmented timing result, frequency tracking may
also score up to eight quarter-clock nominees based on each bit's strongest
ordinary coarse proposal. These nominees stay within the existing clock prior;
the raw tracker also retains up to eight deterministic nominal-epoch cells at
the intermediate clocks and public arrival endpoints. This prevents the noisy
proposal ranking from omitting the configured epoch entirely. Degenerate clock
or arrival axes deduplicate naturally; that raw timing bank is capped at 40.
An additional public midpoint-lag family can contribute at most six coordinates
when the ordinary short tracker and exact integer template layout support it,
including its separable Helper-projected route. It
reuses the clock-zero differential spectra over the transformed public
arrival/clock interval, then maps retained signed lags back to bounded physical
arrival and clock values without rounding them to the old clock lattice. The
frequency tracker reserves a fixed one-eighth share for this family and seven-eighths
for the old family; the new denominator includes all public epochs and lags,
including unselected points. Shared coordinates execute once but retain both
families' separate mass. Eligible empty nomination sets do not reclaim that
share. Publicly unsupported geometry retains the old model and reports why.
The maximum raw union is 46 coordinates, with extra workspace charged before
admission. Expensive tracker count stays bounded, while the input/FFT support
still reflects accumulated clock uncertainty; this is not constant total cost
at arbitrary calendar age. Native FP32/FP64 differential proposals also accept
the existing bulk filter's identical layout; that routing is distinct from an
actual-device qualification. The line-conditioned short model is
compared when its workspace is admitted. Longer coherent-gain and drift models
remain fallback work unless a full comparison is requested. The existing
search, public hypotheses, priors, and `W=2*H` worker budget are preserved.
When a nonzero declared path delay and the existing short-frequency plan are
both supported, the short family also admits a nominal-epoch independent-gain
alternative. It uses at most two coordinates: epoch and capture-clock offsets
zero, at the public arrival lower and upper endpoints, with direct/delayed
paths with zero relative-Doppler offset and the full declared delay. Each path
has an independent proper-complex gain in each short block. The original
unit-power component assigns variance 0.5 to each path. A whole-aperture
mixture also admits total powers `1/16`, `1/256` and `1/4096`, sharing 0.25
mass while the unit component retains 0.75. Each power keeps its own fixed
and moving frequency likelihood until the complete aperture is scored; power
is not reselected block by block. The signed-lag independent branch uses the
same mixture. This reuses correlations, Gram matrices and templates, adding
bounded likelihood state and updates. State and temporary emission storage
are charged. The `independent_gain_emission_cells` receipt counts primary
short-score emissions; a separate line-conditioned rescore is outside that
counter, though included in complete elapsed service. See the
[live-level mismatch and repair](validation.md#weak-combined-modulation-follow-through--17-november-2026).
The signed-lag branch now retains up to four signal-derived maps. The original
sampled two-ray, fixed-frequency nominees remain first. A second proposer
integrates direct-ray evidence across every supported block in the current
aperture, marginalizing common frequency separately in each block. It shares
the old sampled correlations, then visits unsampled blocks once; it does not
search earlier transmissions. It adds at most one nominee per bit and
deduplicates the union. The final two-ray likelihood, public lag/epoch prior
and bit threshold do not change. The full direct rank remains eligible when
the legacy sampling budget admits no fixed-rank blocks. Workspace, operation
arithmetic, worker bounds and cancellation apply before execution; the old
absolute sampling cap does not truncate the new statistic. Complete service
and clock-age scaling remain separate qualification requirements. See the
[matched acquisition investigation](validation.md#ordinary-duration-and-weak-channel-follow-through).
Unsampled direct-rank blocks use exact overlap-save correlation when the bounded
shape estimate predicts at least a 15% FFT-stage reduction. Short FFT tiles
retain the complete template overlap and every public lag, including duplicate
rounded offsets; chunk phase cancels
in the squared correlation. Input transforms are shared across bit/frequency
templates. Sampled fixed-rank blocks retain their original transforms and sum
order. The whole-span transform remains the fallback when a block does not
meet the tile bounds. Lag-order scratch is charged before admission. This changes
correlation work, not the search, prior or likelihood; selected-window baseline
costs and candidate qualification are tracked in the
[calendar-age follow-through](validation.md#persistent-channel-and-calendar-age-follow-through--17-november-2026).
The same projected correlations and Gram matrices feed two equally mixed
components over the full configured frequency range: one integrates a common
frequency held fixed across the whole aperture, and the other uses a bounded
Markov track of changing common frequency. Their independent-gain lattice now
extends the original oscillator-plus-common range by the absolute relative
Doppler allowance, preserving inner nodes and spacing. State vectors and work
are checked and charged; this lane may exceed 65 states. Original common-
frequency families keep their existing 65-state plans. The sampled fixed rank
retains its original range, while the full direct proposer uses the wider ray
envelope; receipts report both frequency counts. This alternative gets
one eighth of the short-family mass and the existing short model keeps seven
eighths at the whole-slot evidence combination, not block by block. That
retention bounds only the old branch's log-e reduction by
`-log(7/8) ≈ 0.134` nats; it is not a dB sensitivity, pointwise-decision or
CPU-capacity bound. Endpoint atoms retain their public timing mass, and
frequency normalization still charges the full declared range; no delay grid
or historical path bank is added. If the projected frequency state plan is
unavailable, the alternative falls back to its central-frequency likelihood;
when `nominal_independent_gain_candidates > 0`, a zero
`nominal_independent_gain_frequency_states` receipt makes that narrower scope
explicit. The frequency planner coarsens its step when floating-point rounding
would otherwise exceed 65 states in the original common plan, preserving both
declared edges. The independent extension adds outer nodes without coarsening
that plan. If the expanded lattice cannot be represented or reaches Nyquist,
it retains the original usable family with explicit
`relative_doppler_extension_unavailable` diagnostics; that fallback does not
qualify the expanded envelope. Unsupported base
plans add no branch, leaving Off, medium and slow
VLF paths unchanged. Public bounds do not guarantee that actual channel
behavior fills the bound. The finite source-bound full-stream cohort passes:
both matched seed-9202 polarities and both fresh seed-9601 polarities decode
5/5, while the seed-9602 noise control remains 5/5 QUIET. This supports the
sampled standard-Gaussian histories only, not all plasma, arbitrary clock-age,
all-delay or weak-Helper cases. At the prior checkpoint, the two scoped
positive-delay Helper checks passed, GUI passed 28/28, and appraisal/application
checks passed (5/5 plus integration). Those checks predate the later receiver
patch; final-source group outcomes are recorded below. These are scoped software
checks, not broader channel-sensitivity qualification. See the
[source-bound HF Worst follow-through](validation.md#hf-worst-30-hz--7-ms-follow-through--16-november-2026).
This is a bounded receiver policy, not authentication or a claim that all
wrong-bit cases are resolved. See the dated
[short-model and clock-support findings](validation.md#bounded-short-model-competition--16-november-2026)
for source-bound outcomes and measured cost limits.

The existing coherent drift family also evaluates both signs of nonzero
relative Doppler. It evaluates the original sign first, then its mirror using
the same bounded workspace, and mixes them with equal prior weight inside the
unchanged outer family. Both retain the original common-frequency, slope,
power and full public timing priors. Selected timing-atom normalization is
undone separately before mixing and restored for the shared outer calculation;
overlapping timing coordinates remain distinct half-weighted sign atoms.
Zero Doppler keeps the original single call. Retained-line scoring uses the
same wrapper, and work/time include both evaluations. No additional historical
search or independent two-frequency grid is introduced. See the
[matched negative-Doppler failure and repair](validation.md#persistent-channel-and-calendar-age-follow-through--17-november-2026)
for the source-bound reception and complete-service costs. Earlier CPU
calibration does not qualify this changed work.

Drift timing selection retains all eight segmented-ridge winners and adds at
most one normalized whole-aperture, zero-slope coherent winner per bit, with
deduplication and deterministic ordering. The two statistics select their own
winners; their numerical scales are not combined. This preserves useful calm-
channel evidence that the ridge ranking can otherwise discard. The timing
union and full public prior remain unchanged. A bounded per-call peak-frontier
cache reuses the zero-slope FFT work during exact scoring; it stores neither
spectra nor templates for every timing and fits inside the existing metadata
reservation. Signed and line-conditioned inputs retain separate computations.
The per-timing slope budget depends on FFT size, not the number of retained
timings or parallel lanes. Adding coherent nominees therefore does not remove
nonzero slopes from earlier ridge winners. The selected-count averaging and
outer refund still cancel against the same full public timing prior.

The coherent planner treats its 262,144-point FFT as a preferred size, not an
absolute duration limit. If that shape cannot preserve the declared frequency
bandwidth, it may retain a faithful stride with a larger checked transform
(including stride one). The extension is fixed before looking at received IQ.
Ordinary and retained-line routes are admitted independently against **both
unchanged baseline sequential and overlapped full-worker workspace quotes**,
including dependent projection/coarse work. A rejected extension preserves the
original family membership and priors; `drift_status` / `drift_line_status`
report the chosen preserved-lease boundary. That status does not imply exhausted
machine RAM or equal sensitivity under every explicit worker allocation.
Before rejecting an expanded route, its planner selects the largest stage width
from one through eight (also capped by owner workers) that fits those same
quotes. Only that new coherent stage's parallel width changes; global W, other
stages and all timing/frequency hypotheses remain intact. This avoids dropping
the model when a balanced automatic owner receives fewer workers. Planned and
executed stage widths are reported separately from global/owner allocation.
The extended route retains at least three slope proposals, including zero and
one best proposal per bit, without reducing the public slope prior. These
rules prevent the observed long-bit model exclusion without lowering the
existing ready-owner capacity. See the
[long coherent observation checks](validation.md#long-coherent-observations-and-the-fft-planning-boundary).

The base-`6ac502a` late-clock baseline exposed a separate work ceiling: a
slot-12, ordinal-12 severe long bit with ±150 ppm public clock support predicted
41.8M exact-column templates and was refused at its old 30M hard limit before
IQ scoring. Current production removes that absolute refusal while retaining
the mapped-route heuristic and 120M mapped-route limit, full workspace/FIR/grid
checks and pre-DSP cancellation. A measured production build with the bounded
signal-derived clock proposer scored the original two full-capture hashes
without overrides: DATA bit 0 at 0 ppm (69.873, 41.269 s) and −100 ppm
(63.708, 46.098 s); the older fixed-only run was ambiguous on the latter
capture. The 548-lag × 13-CFO
proposal ranks in 0.303/0.346 s and the pair totals 87.384 s. A 278.4-second
bit preparation covers all 156 columns under the 3-GiB DSP lease, but stops
before IQ scoring. Evaluator-known timing remains oracle diagnostic evidence.
Ordinary fine scoring uses the fixed-affine `FineLanes` branch and skips its
adaptive per-block branch; other separately admitted channel alternatives are
unchanged. Adaptive mode remains explicit for diagnostics. Strict capacity and
forecast consumers require `adaptive_diagnostics=false`, null adaptive
lane/score fields and zero adaptive counters. The fixed-only Helper replay
preserves selected DATA and QUIET outcomes to 12 significant digits, with
slightly slower slot times. The clock-pair binary predates final delay-tier
removal; final-source triad, GUI, appraisal and repeated separate-transmission
checks passed at that historical checkpoint as recorded in validation. Capacity remains unqualified, and these probes do not
qualify long-run reception or general availability.
The previously reported +12 ambiguous / +20 DATA delay pair used a fixture
without the canonical 0.25 s preroll: its direct-path start fell outside the
declared arrival bounds and establishes no in-bound delay floor. The frozen
source now passes corrected canonical 15.75/5.25/10.5 ms bit-0 apertures,
15.75 ms bit 1 and true absence QUIET, each at +12 dB-Hz with the direct start
inside public rows 480–499.2. A separate age-1,800 s / ordinal-12 case at
−100 ppm and 15.75 ms also scores DATA with matched noise QUIET. These finite
single-bit outcomes also survive a bounded common-frequency motion replay
(log-e 37.8052 vs 21.493) with sampled Doppler −29.83236…+29.84666 Hz
and unchanged noise/energy. On the frozen source, triad passes 40/40, GUI
28/28 and appraisal 5/5; the candidate-only five-attempt-per-polarity replay
also passes (see the [source-bound record](validation.md#final-source-repeated-long-alarm-check--16-november-2026)).
These finite outcomes do not qualify full text messages, capacity or physical
availability.
The earlier weak-Helper/common-frequency tests passed selected synthetic cases
with extra work when a stale interval missed DATA. A weaker matched observation
then lost the bit despite identical DATA and noise. Helper frequency transfer
now accounts for the maximum change of the declared common excursion between
the Helper and DATA windows, after pilot consistency checks. It uses twice the
larger of the common and atmospheric allowances, converted through the slowest
allowed receive clock, and clips to the original coherent DATA frequency prior.
Existing broad/empty-interval fallback restores the full prior; a useful narrow
interval remains available when the motion allowance permits it. No timing,
phase or gain is transferred, and no second coarse pass is added. When the
clock-adjusted excursion covers the entire coherent frequency bound, a shared
planning/execution predicate omits the necessarily non-narrowing Helper fit
and its scratch in the production driver, reporting
`common_motion_requires_full_data_frequency_prior`. Input/mask validation,
DATA filtering, full prior and worker allocation remain intact; direct Helper
diagnostics still measure the pilots when requested. See the
[matched repair and costs](validation.md#weak-helper-follow-through--17-november-2026)
and the [earlier Helper record](validation.md#late-clock-and-interior-delay-probes--16-november-2026)
for source-bound outcomes and limits.
The [historical repeated-transmission diagnosis](validation.md#repeated-single-bit-channel-diagnosis--16-november-2026)
records the nominal-epoch omission and subsequent calendar-age clock-resolution
diagnosis motivating these two bounded timing supplements. Sequential and
overlapped Helper preparation quote and pass the same midpoint work. The late
Helper regression covers attempted acquisition with projected DATA fallback;
it is distinct from a new successful-acquisition or field qualification. Mapped
template layouts without exact unit-step support report the unavailable family.
Differential and segmented spectral statistics propose candidates; they are
not admission statistics. Retained candidates keep their original public
prior mass, and a skipped model does not transfer its weight to another model.
The frequency mixture retains a small state vector instead of enumerating
histories. Final likelihoods and sensitive state use FP64; reduced precision is
used only for guarded proposal work. Whole-slot drift proposals use reduced-rate
matched products followed by tiled exact scoring; neither a raw-row cutoff nor
a fixed ridge-segment count excludes long captures. Public work, sample and
alias bounds still apply. Drift proposals refine frequency/slope basins before
spending their bounded exact-scoring work. Segmented timing reuses observation
transforms across the public hypotheses. Broad searches retain fast proposals
and add a slower coherence scale with bounded frequency-path proposals when
admitted, retaining only two score planes rather than enumerating histories.
Unrestricted block-frequency peaks
remain an alternative. Proposal spacing follows coherent duration, not padded
arrival uncertainty; its work reference uses the combined oscillator and
atmospheric support. All public arrival, epoch, clock and center-frequency
coordinates remain available. Extra proposal state is included in the admitted
workspace. Status receipts distinguish supported, unnecessary and guarded
paths; these finite models do not cover every bounded physical trajectory.

Changing-channel admission also requires separation between the operational bit
scores. Its public policy derives `beta=(1-p)/(2*N)` from the duration planner's
fixed planning probability and bit count, then requires `log((1-beta)/beta)`
separation (5.2933 nats at the default p=0.99, N=1). These are public assumptions,
never received message lengths. Strong DATA presence with insufficient bit
separation remains an erasure and allows later models to run; it cannot count
as physical quiet. This is a conservative decision policy, not a qualified bit
error probability or an exact posterior bound for data-selected sub-mixtures.
A proper single-line nuisance alternative projects both observations and DATA
templates before its conditional rescore. It keeps the line factor in both
numerator and denominator, permitting real DATA alongside a coherent interferer;
this is not a general interference model.

Quiet evidence uses complete orthogonal blocks wholly inside the native public
aperture, bounded by neighboring possible DATA support. No received length or
codec field controls DSP geometry, allocation or physical completion. Selected
changing-channel modes also require absence in the unnotched expanded band;
an unavailable expanded-band check cannot confirm quiet. The GUI
plots actual operational bit alternatives, separately from affine and adaptive
diagnostics. See [changing-channel behavior](channel-models.md#robust-gui-changing-channel-realization),
[research and contrary findings](triad-stochastic.md#nonstationary-channel-research-reconciliation)
and the current [validation record](validation.md).

## GUI settings and operating limits

The shared profile maps encryption, Helper power/ratio/callsign, DATA transmit
power, path loss, clock policy, oscillator models, spreading, burst waits, FHSS
and channel controls to the native components. Rate is the native inner rate;
Added Symbols and Interleave select native lanes and spreading. Robust Setup
applies the selected minimum of 0, 125, or 265 Hz to adjacent DATA carrier
centers, defaulting to 265 Hz. The nominal span includes the two center gaps
and one pulse bandwidth. Both peers need the same effective spacing. Invalid audio
bandwidth or fixed Helper placement fails configuration explicitly. None/Off
channel choices preserve an explicit unperturbed baseline.

Carrier recommendations and the Link planner use the native three-carrier
DATA width, including the RRC rolloff and the minimum public tile's certified
gate allowance. The receiver and Helper placement use the same calculation
for the selected transmission waveform. The planner also shows the DATA
edges with the configured oscillator and channel allowances. A recommended
audio carrier needs a lower edge of at least 375 Hz, or 1400 Hz with Helper.
The half-rate preset is exempt from that recommendation floor and still has
to fit the physical DATA and Helper bands. Preset values are baseband
frequencies displayed as absolute Carrier after adding Shift.

Target SNR is one shared C/N0 value, default 32 dB-Hz. It drives provisional
native duration planning and, with the declared input-noise density, the live
receiver's reference DATA amplitude. The simulated link's actual
transmit-power/path-loss/noise-density calculation remains separate. Planning uses a fixed
public message assumption rather than the draft length. A predicted duration
is not a sensitivity guarantee or a real-time capacity result. FHSS profiles
control the canonical envelope and illustrative schedule; they do not retune
external radio hardware. See [duration planning](triad-duration-planning.md),
[burst scheduling](symbol-burst.md) and [channel models](channel-models.md).

The target is a weak-signal design point, not a required received level or an
upper amplitude limit. Peers agree on waveform settings; actual signal strength
and channel evolution may differ from those assumptions. Qualification must
hold receiver gain/noise and public bounds fixed while varying actual signal
gain independently. A simulated C/N0 change that also recalibrates the receiver
does not establish that tolerance. Hardware clipping and analog input range
remain separate from the native detector's tested range.

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
and complete native score records. Finite Robust simulations generate, pass and
score the full native stream without a wall-clock sample delay. The presentation
retains three seconds of actual channel I/Q from the first DATA pattern, or
from the first Helper onset on a Helper-only transmission, plus one FFT window
of lookahead. After the receiver drains all available physical windows, the
app replays this excerpt at its normal sample cadence and retains the plots.
It computes the display projection and FFT during replay. Stopping that replay
preserves any already completed reception. Live audio and the open-ended noise
display keep their real-time cadence. Audio routing, duplex mode, TX volume and
exclusive mode use the existing native audio interface; physical device behavior
requires its own qualification.

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

All three DATA carrier lanes bind the configured nominal physical frequency
(baseband plus Shift) through the `independent-apsk-v2-carrier-v1` derivation.
Its rounding step is `max(10 Hz, Rate × Spreading / 10)`; hops and receiver
corrections never change the key context. Peers need matching rounded frequencies
and the updated derivation. Same-bucket settings deliberately remain equivalent;
this is waveform separation, not sender authentication. See the
[canonical key context](crypto.md#canonical-robust-waveform-purposes).

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

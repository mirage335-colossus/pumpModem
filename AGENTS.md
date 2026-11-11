# Development requirements

Use the [documentation index](docs/README.md) for maintained guidance. Check
[the development contract](docs/development.md), [build and test stages](docs/building.md#testing-stages),
and [current Robust architecture](docs/robust-current.md) before changing those areas.
Search `src/`, `include/`, `tests/` and `cmake/` first when locating implementation.

## Prioritize receiver computational ceiling in automatic mode

**Automatic mode must prioritize complete-receiver throughput and DSP workspace
capacity on challenging cases, to prevent reception failure from accumulating
input backlog.** Treat computational ceiling as the primary design objective;
easy-case efficiency, energy savings and unnecessary numerical precision must
not lower it. This applies to CPU and GPU algorithms, arithmetic, layouts,
batching, preparation overlap and worker ownership, not just numeric types.

The automatic option is the user's way to obtain the best supported throughput
without selecting hardware-specific kernels. Higher-precision GUI overrides do
not excuse conservative automatic defaults. Prefer suitable lower precision;
retain higher precision where evidence shows a worthwhile reception, range,
conditioning or complete-service benefit, or no plausible computational benefit
from reducing it. FP64 agreement at a universal tolerance, an arbitrary dB loss
cutoff, or no gain on one easy fixture must not veto a candidate with a credible
challenging-case advantage. Investigate unrealized hardware benefits and fix
implementation/dispatch bottlenecks before declaring a hardware ceiling.

Follow the authoritative [automatic-mode contract](docs/gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling)
for proportional sensitivity/cost tradeoffs, bounded evidence and fallback
reporting. Preserve all public hypotheses, configured-calendar behavior, physical
quiet-slot completion, explicit resource limits and the full `W=2*H` budget.

## Keep intensive processing in the native application

**Prefer C++ and compatible in-process native libraries/backends for modem,
signal processing and other intensive numerical work in this native application.**
Extend the canonical compiled stack; do not introduce a Python DSP implementation
or split the operating signal chain across Python/native processes for scripting
convenience. Helper, DATA, receiver state, codecs and audio belong in the native
application, with shared buffers, explicit ownership and coordinated workers.
Choose batch interfaces and layouts that support CPU threads, GPU/low-precision
work, and overlap with preparation where dependencies permit. Python may launch
the native tests, collect measurements and analyze results; it must not own modem
algorithms or persistent receiver state. Cloud tests must exercise that same
compiled implementation. Follow the [native processing contract](docs/development.md#native-processing-and-integration)
before selecting a runtime, parallelism mechanism or accelerator interface.

## Prefer modern GPUs for accelerated development

**Default GPU development, accelerated tests and GPU-assisted forecasting to a
full NVIDIA RTX PRO 6000 Blackwell, or a demonstrably more suitable modern
accelerator.** Prioritize rapid qualified testing and demonstrated high utilization
of highly parallel, lower-precision and Tensor Core hardware. Energy efficiency
is not this project's objective. Do not default to A100/A5000-class hardware
because it is familiar, cheaper per hour or named in an old proposal. A different
choice needs a recorded workload, compatibility, availability or budget reason.
Verify the exact generation, edition and full-device allocation; provider labels
such as `WX`/`WK` are not sufficient identification, and a MIG partition is not a
full GPU. Follow the [GPU selection contract](docs/triad-performance.md#gpu-selection-for-accelerated-development)
before choosing or renting hardware. Preserve spending authorization and bounded
testing; this preference is not a universal speed claim or permission to spend.

## Cover representative receiver hardware

Performance tests must support the [representative receiver targets](docs/triad-performance.md#representative-receiver-hardware):
RTX 4090 desktop, RTX 4090 Laptop, RTX 4060 Ti **16 GB**, and Ryzen 5 PRO
5650U / Ryzen 7 8840U CPU and integrated-graphics systems. These operating
targets do not replace the development-GPU preference. Record the actual
processor, memory, runtime and executed arithmetic; a target label, compatible
build or CPU fallback is not a GPU measurement. Keep unavailable AMD GPU paths
explicit. Qualify low precision after Helper/DATA channel isolation separately
from raw input and sensitive state. An INT8 matrix contraction is not an INT8
FFT. Choose defaults by complete service on challenging broad searches, including
unsuccessful Helper acquisition, while preserving every public hypothesis and
the full `2*H` worker budget. A narrow or easy-case win cannot lower that ceiling.

For the 5650U's Vega-class Radeon, use the optional
[native Vulkan route](docs/triad-performance.md#local-radeon-vulkan-processing)
when qualifying its actual GPU. Do not infer unavailability solely from ROCm or
OpenCL support, or count `llvmpipe` as GPU execution. Preserve explicit native
stage routing and shared-RAM admission. Packed FP16 vector arithmetic is distinct
from FP16 storage and from newer AMD matrix hardware; report measured speedup
separately from the [conditional arithmetic ceiling](docs/triad-performance.md#radeon-arithmetic-ceilings).

## Preserve the full receiver CPU budget

Default Robust receiver orchestration and capacity testing to **twice the
available logical hardware-thread count in software workers**, using all hardware
threads while respecting explicit allocations and actual OS affinity/quota
limits. Do not reduce this default because of thermal or package
power limiting, low observed utilization, a single-machine pilot winner, or
temporary/minor timing differences. Preserve capacity for incoming sample
backlog, other processors and changing workloads. Smaller stage dispatch is
allowed when useful work is limited; it must not become a reduced global
worker default. Follow the [CPU allocation contract](docs/development.md#receiver-cpu-allocation).

## Manual tests target practical local CPU capacity

The editable `_tests-RobustModem.md` asks what this computer's **complete
receiver can sustain in real time**. Before using it, follow the
[agent workflow](docs/robust-manual-capacity.md#agent-workflow-for-cpu-ceiling-estimates).
Use efficient receiver algorithms and productive same-stream parallelism;
investigate serialization instead of treating it as the CPU's hardware ceiling.
Prefer qualified lower-precision and tensor-compatible GPU work with narrow,
sustained local CPU phase calibration and fresh holdouts. Follow the
[precision guide](docs/gpu-precision-controls.md) and reuse applicable evidence
through the [GPU-assisted procedure](docs/triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast).
GPU speed or a hypothetical optimization is not a CPU capacity result. Report
complete nominal and conservative service, uncertainty, margin and unresolved
work for every current row; a forecasting gate is distinct from a capacity pass.
Preserve the receiver scope: current complete-work calibration assumes a
configured public calendar and keyed ordinal sequence. Acquisition of unknown
waveform epochs, ordinals and timing remains unqualified. The receiver detects
bits or nulls continuously; two scored quiet slots confirm message completion,
and codec reset preserves the global waveform ordinal. No semantic transmitter
identity classifier is required. Broader acquisition needs appropriate address
hypotheses, incorrect-address checks and measured costs; a favorable scoped
estimate cannot remove this missing work. See the
[current findings](docs/validation.md#continuous-receiver-restructuring).
The operating stack is one compiled C++ modem linked into the GUI binary, with
explicit Helper, DATA, receiver and audio interfaces. Python may test and
benchmark the native implementation but is not in the operating path. The
continuous native receiver overlaps ready windows with independent in-process
scorer owners sharing immutable input buffers, and commits public slots in
order. Resolve the automatic software budget once as twice effective
hardware-thread capacity. Slot roots and coarse/fine work share one bounded
receiver executor; per-owner admitted lane/scratch shapes are full DSP bounds,
not exclusive worker permits held for a whole slot. Quote the largest ready
prefix with full-`W` scratch per owner; prefer an admitted balanced shape only
when it admits a strictly wider ready prefix. Keep each shape fixed through its
slot. Ready leaves prioritize the oldest eligible public slot, while queued roots use unneeded
lanes. The aggregate `W` ceiling does not change.
Started idle pool stacks remain charged until close. Explicit worker, owner and
memory limits remain exact, with owner count at most `W`. Do not impose this
computer's old three-owner, 3840 MiB or half-available-RAM settings as portable
limits. Charge application baseline, actual live owners, stacks, full DSP bounds,
input assembly/storage and retained results throughout their ownership lifetimes.
Completed out-of-order windows may free owners before ordered commit; bound
retained results separately. There is no receiver subprocess, bootstrap, IPC or
memfd operating contract.

Keep software permits separate from the hardware utilization denominator.
Qualify changes with bounded same-stream work, throughput and memory evidence,
using predeclared backlog intervals separately from startup/fill/drain averages.
Higher RAM consumption and modest productive utilization gains are worthwhile;
energy efficiency is not this capacity goal. The historical three-controller
comparison missed its 15% throughput gate but improved useful throughput and
utilization; the user's later adoption of that smaller gain does not change
the original failure. Historical Python process-runtime calibration and GPU
forecasts do not qualify the new native scheduler. Preserve original margins,
failures and applicability limits in the validation record, and refresh affected
local costs before a native capacity claim. Follow the
[current capacity workflow](docs/robust-manual-capacity.md).

## Prevent accumulation

Remove obsolete implementations and their tests, copied source, old performance
code, redundant historical evidence, abandoned experiments and stale builds as
part of the work that supersedes them. Do not keep them as examples or for
speculative future use. Preserve potentially useful alternative algorithms and
test conclusions as concise findings, not as old implementations or campaigns.

Treat experiments, captures, logs and generated outputs as temporary, including
ignored files and cloud volumes. Keep only what serves a concrete current
investigation, validation claim or required release record, and identify its
purpose and cleanup trigger in the relevant existing guide or validation entry.
When that purpose ends, distill applicable algorithm choices, negative results,
assumptions and provenance into concise guidance, then remove redundant outputs.
Do not keep whole campaigns merely because they might someday be useful, or
archive obsolete implementations, copied source trees or standalone variants.
Keep an indispensable dataset only when a specific current question cannot be
answered by the summary; explain why. Preserve evidence needed for unresolved
work, failed checks and release assurance until its purpose is resolved.

Reuse compatible build profiles and caches that current work needs; retire
abandoned profiles and stale outputs for removed targets after checking for
active users. Do not routinely purge useful caches or other contributors' work.
Apply the same lifecycle to local and remote storage. See the
[development contract](docs/development.md#material-lifecycle) and
[local output guidance](docs/building.md#local-output-retention).

## Robust architecture and intensive checks

The canonical Robust stack is native `IndependentApskV2`. Its C++
`CompleteStreamReceiver` connects noisy Helper acquisition, full public FFT
proposals, fine persistent tracking, bit/null decisions and scored quiet-slot
completion to bounded native short/long-message and configured FEC processing.
The shared GUI session links this same stack and exposes configured-calendar
operation. Source generation, DSP, persistent receiver state and real-audio
projection are C++. Python tools only orchestrate native probes and analyze
statistics; the former Python numerical receiver implementations are retired.
The default application needs neither Python nor Torch. Optional compiled ATen
and Vulkan bulk operations share the production receiver control and bounded
ownership. Requested CUDA must execute on CUDA or report unavailable; a required
Vulkan accelerator result must identify actual GPU search work, with native
stage routing reported explicitly.
Implementation and component parity do not qualify all configurations,
communication success probabilities, real-time capacity or physical hardware.
Keep current checks and remaining gates accurate in
[validation](docs/validation.md#native-codec-and-measurement-migration).
Historical Python costs and transport exceptions cannot calibrate the native
runtime; changed CPU work requires fresh affected timings and holdouts.

Use one in-tree implementation. Protocol changes evolve the canonical source;
maintain older protocol compatibility with Git tags and separate binary releases.
Backport a necessary security fix narrowly to strictly sanitized dynamic-input
surfaces.

Computationally intensive Robust tests and performance estimates must use a
hierarchical, computationally minimal statistical method. Predeclare the covered
path strata, sample budget, uncertainty decision and stopping rule; use cheap
deterministic checks first. Extrapolate only from abbreviated measurements when
fixed-geometry assumptions, uncertainty, and source/configuration provenance are
recorded and applicable. Do not use full, sustained, multi-day, exhaustive, or
whole heavy-calibration runs as routine tests or fallbacks. RunPod may accelerate
the same method but does not justify larger experiments. See
[the maintained policy](docs/robust-current.md#testing-computationally-intensive-robust-changes)
and [the abbreviated harness](docs/triad-performance.md).
Run `./build.sh test triad` before refreshing local calibration with
`tools/triad_manual_cases.py`. Changed CPU work or runtime needs fresh affected
local timings; unchanged applicable calibration remains reusable.
Manual cases are absolute complete-receiver capacity tests, distinct from the
usual abbreviated performance regressions that look for degradation. They share
the efficient sampling method, not the meaning of a passing result. Follow
[the complete-work contract](docs/robust-manual-capacity.md): include fine search,
tracking, unsuccessful candidates, quiet-slot completion, reacquisition, recurring
preparation and orchestration. Measure productive parallelism within one received
stream on the local CPU, including its physical-core and full logical-CPU
allocations. Headless measurement commands default to the full visible logical
budget, with one numerical thread per receiver worker and explicit overrides.
Independent simultaneous simulations are not receiver throughput.
Missing or unmeasured required work must prevent a complete-receiver capacity pass.
Nominal service uses a brief uninterrupted fine session with a fixed two-second
prefix and 1/2/4-second continuation, verified cache fills and state/holdout gates.
Charge recurring work and prefix/finish once per slot. Do not infer a warm-up
benefit from fresh short sessions or replace the separate worst-state bound with
a nominal estimate. A numerical estimate at least 1 s/s is a point failure even
when its extrapolation uncertainty prevents a stronger capacity conclusion.
It reads `_tests-RobustModem.md` by default, requires all 16 strict fields,
preserves ordered duplicate rows, and reuses identical normalized inputs only
when explicitly marked. Use repeatable `--case-index N` for explicitly scoped
work. The full list is still parsed and validated, and partial output covers
only selected cases. Predeclare separate complete-receiver sensitivity checks
and keep those communication trials separate from CPU-capacity measurements.
Historical coarse-only sensitivity and isolated tracking results do not qualify
this complete-receiver contract. A prior comparison of numerical-library thread
counts is not evidence about parallel hypothesis/candidate processing. Preserve
explicit allocation controls and selected native channel effects; timing
replicas are not independent success trials. Keep raw case JSON/report
temporary in ignored `local-evidence/` and remove them when their current
validation purpose ends.

## Shared contracts

Robust and Fast finish messages from scored physical absence in quiet slots.
They must never use received packet lengths or arbitrary-length headers to select
DSP geometry, allocations, or physical completion. Transport EOF, cancellation,
local quotas and application metadata are not successful receiver completion.
Preserve fixed public geometry and local resource bounds; interior missing slots
remain erasures when transmission resumes. See the
[physical-completion contract](docs/robust-manual-capacity.md#physical-completion-and-security).

Preserve received-content restrictions and input validation in
[security](docs/security.md), [receive processing hardening](docs/receive-processing-hardening.md),
and the [GUI contract](docs/gui-contract.md). Keep GUI behavior shared across
native and optional frontends. Preserve Fast and Legacy behavior under their
[topic guidance](docs/README.md#other-maintained-components); do not infer their
compatibility from Robust research.

Before changing simulated channel paths, read the
[Doppler/delay-spread guide](docs/channel-models.md) and preserve selected effects,
continuous state, delayed tails, and explicit Off baselines. Before changing GPU
precision controls, follow [the precision guide](docs/gpu-precision-controls.md)
and retain explicit fallback reporting.

## Validation and outputs

Use `./build.sh` and named groups in [the build guide](docs/building.md). Run the
smallest relevant correctness checks during diagnosis, then the applicable
affordable checks. Never weaken contracts or report omitted or incomplete
coverage as passed. Documentation-only changes need proportionate link and
consistency checks, not receiver benchmarking.

When release assurance is in scope, follow [release validation](docs/releases.md)
and record current outcomes and limits in [validation](docs/validation.md).
Certification binds to exact source and binary hashes; publication, certification,
and promotion are separate operations.

Preserve unrelated edits and other contributors' work. Coordinate shared Git
changes and timing-sensitive tests. Keep temporary evidence in ignored
`local-evidence/`; maintained contracts and current validation belong in `docs/`.
Do not create session or coordination registries.

# Documentation topic index

Start with maintained contracts and current architecture. Historical findings
remain useful only within their stated source, input, configuration and
measurement limits.

Follow the [material lifecycle](development.md#material-lifecycle): distill
useful findings, keep only evidence with a concrete current purpose, and retire
obsolete code, experiments and generated outputs when that purpose ends.

## Current Robust modem

**Automatic CPU/GPU choices must prioritize challenging-case receiver capacity
and prevent input backlog.** Start with the
[automatic-mode contract](gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling)
before choosing algorithms, arithmetic or scheduling defaults. Easy-case
efficiency and unnecessary precision must not lower that computational ceiling;
explicit precision overrides serve exceptional needs.

For new modem, DSP or intensive numerical work, follow the
[native processing contract](development.md#native-processing-and-integration):
prefer the integrated C++ implementation and compatible native backends; Python
orchestrates tests and analysis rather than the operating signal chain.

The [current architecture and status](robust-current.md) describes the native
C++ Helper/DATA/complete-receiver stack linked into the GUI, with shared buffers
and coordinated in-process workers. Python orchestrates native tests/benchmarks
and analyzes statistics; it does not implement receiver DSP. The default
application needs neither Python nor Torch; optional compiled ATen and Vulkan
backends share production receiver control. The [GUI contract](gui-contract.md) maps the controls
and requires a shared public calendar and keyed ordinal for live operation;
unknown-start acquisition remains outside scope.

See [native migration validation](validation.md#native-codec-and-measurement-migration)
for current checks and limits. The earlier short-profile GUI checkpoint does not
qualify later long-message, audio-frontend or benchmark changes. Historical local
CPU margins, GPU comparisons and process-scheduler measurements retain their
original source and execution mode in the [validation record](validation.md).
They do not qualify native GUI capacity. The
[interactive parallelism report](receiver-parallelism.html) shows that historical
runtime, including its failed exact tracking-task gate and separate post-hoc
comparison. Its measured process-counter timelines are neither the current
in-process scheduler nor proof of full CPU saturation.

Retained [pattern research](triad-patterns.md),
[stochastic channel findings](triad-stochastic.md),
[rate-aware shaping](triad-shaping.md), and
[channel model contract](channel-models.md) provide bounded technique summaries
and evidence limits. New native CPU work needs applicable local phase timing;
source-matched numerical parity alone is not performance calibration.

Computationally intensive Robust checks must follow the hierarchical,
computationally minimal statistical method in [the current testing policy](robust-current.md#testing-computationally-intensive-robust-changes).
The [native regression policy](triad-performance.md) defines checks for
degradation against a matched baseline. The distinct
[manual-case capacity contract](robust-manual-capacity.md) requires complete
receiver work, productive local CPU parallelism and absolute real-time/workspace
margins for the freshly read manual configurations. **Agents using
`_tests-RobustModem.md` must follow the [CPU ceiling workflow](robust-manual-capacity.md#agent-workflow-for-cpu-ceiling-estimates):
assess practical complete-receiver feasibility on this CPU using efficient
algorithms, qualified reduced precision and all available hardware threads.**
The [precision guide](gpu-precision-controls.md) distinguishes current fast
arithmetic from tensor-compatible improvements requiring qualification.
The [representative receiver hardware](triad-performance.md#representative-receiver-hardware)
contract separates Ada desktop/laptop and Ryzen CPU/integrated-graphics targets
from the preferred development GPU and from actual measured devices.
The [local Radeon Vulkan path](triad-performance.md#local-radeon-vulkan-processing)
and [arithmetic ceiling model](triad-performance.md#radeon-arithmetic-ceilings)
keep native GPU results separate from processor peaks and CPU-only evidence.
The manual suite and regression harness share efficient
sampling methods; their pass decisions are different. Run `./build.sh test triad`
before manual capacity measurements. Earlier partial-stage results do not
qualify complete reception or whole-machine capacity.
For repeat manual checks, start with a GPU workload trace and applicable local
phase calibration. Reuse qualified GPU references and measure missing CPU cost
classes or fresh bounded holdouts locally. Headless measurement commands use the
portable oversubscribed receiver worker budget by default and preserve explicit overrides. The
[CPU allocation contract](development.md#receiver-cpu-allocation) prohibits
reducing that default because of power/thermal limits or minor timing effects. Follow
the [GPU-assisted procedure](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast).

## Other maintained components

| Topic | Start here |
| --- | --- |
| Product and operation | [Project overview](../README.md), [requirements and limits](requirements.md), [protocol](protocol.md), [manuals](#command-and-interface-manuals) |
| Development and delivery | [Development contract](development.md), [build and test guide](building.md), [release procedures](releases.md), [offline installation](offline-installation.md) |
| Helper Carrier | Native generation and acquisition share the [Robust stack](robust-current.md); historical measurements retain their original scope |
| Simulation channels | [Doppler, delay spread, selective amplitude and FHSS tails](channel-models.md) |
| GPU hardware and precision | [GPU selection contract](triad-performance.md#gpu-selection-for-accelerated-development): prefer a full RTX PRO 6000 Blackwell or a demonstrably more suitable modern accelerator; [representative receiver targets](triad-performance.md#representative-receiver-hardware) and [precision guidance](gpu-precision-controls.md) define qualification |
| Fast Modem | [Fast topic index](indexes/fast.md) |
| Legacy Modem | [Legacy reference](legacy-modem.md) and retained interoperability fixtures |
| GUI and frontends | [Interfaces index](indexes/interfaces.md), [GUI contract](gui-contract.md), [frontend interfaces](frontend-interfaces.md) |
| Security | [Security index](indexes/security.md) and [receive processing hardening](receive-processing-hardening.md) |
| Current validation | [Validation record](validation.md) and [evidence index](indexes/evidence.md) |

## Command and interface manuals

| Manual | Interface |
| --- | --- |
| [pump-fast(1)](man/pump-fast.1) | Fast command-line operation |
| [datapump-gui(1)](man/datapump-gui.1) | Native graphical application |
| [datapump-tui(1)](man/datapump-tui.1) | Terminal application |
| [datapump-fb(1)](man/datapump-fb.1) | Framebuffer application |
| [datapump-worker(1)](man/datapump-worker.1) | Pipe worker |

See [reading manuals offline](offline-installation.md#manual-pages) for installed
and source-tree usage. `pump(1)` now documents keyfile, audio-device, QR and
Fast utility commands; Robust audio commands are retired.

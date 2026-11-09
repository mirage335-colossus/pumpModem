# Documentation topic index

Start with maintained contracts and current architecture. Historical findings
remain useful only within their stated source, input, configuration and
measurement limits.

Follow the [material lifecycle](development.md#material-lifecycle): distill
useful findings, keep only evidence with a concrete current purpose, and retire
obsolete code, experiments and generated outputs when that purpose ends.

## Current Robust modem

The [current architecture and status](robust-current.md) identifies the
canonical native waveform/channel/Helper stack and the complete headless
receiver. Its frozen native message gate passed, including the captured-prefix
EOF negative control. The current full eight-row CPU suite has all eight
nominal conditional points below 1 s/s, but six conservative cold points fail
that threshold and all stronger capacity decisions remain unresolved. The
12-thread CPU ceiling was not demonstrated: continued fine work used about
1.8–2.0 busy cores, and the 12/6 fine comparison showed no consistent gain.
The RTX PRO 6000 diagnostic demonstrated faster coarse and continued-fine
stages but does not provide a local CPU conversion factor. Sensitivity
confirmation remains incomplete without a >90% qualification. See the
[validation record](validation.md) for provenance and limits.
Robust remains unavailable in the GUI pending
reconnection. Retained
[pattern research](triad-patterns.md), [stochastic channel findings](triad-stochastic.md),
[rate-aware shaping](triad-shaping.md), and [channel model contract](channel-models.md)
provide bounded technique summaries and evidence limits.

Computationally intensive Robust checks must follow the hierarchical,
computationally minimal statistical method in [the current testing policy](robust-current.md#testing-computationally-intensive-robust-changes).
The [isolated triad regression policy](triad-performance.md) defines checks for
degradation against a matched baseline. The distinct
[manual-case capacity contract](robust-manual-capacity.md) requires complete
receiver work, productive local CPU parallelism and absolute real-time/workspace
margins for the freshly read manual configurations. They share efficient
sampling methods; their pass decisions are different. Run `./build.sh test triad`
before manual capacity measurements. Earlier partial-stage results do not
qualify complete reception or whole-machine capacity.
Use the full-logical inference option to report a fresh 12-worker run after
the same-stream 1/6/12 allocation pilot. RunPod may assist development and
workload exploration, but only local source-matched timings measure this
computer's CPU service. Use authenticated SSH/SCP for source and binary transfer;
the provider console is for pod lifecycle. See the [manual capacity
contract](robust-manual-capacity.md) and [GPU procedure](triad-performance.md).

## Other maintained components

| Topic | Start here |
| --- | --- |
| Product and operation | [Project overview](../README.md), [requirements and limits](requirements.md), [protocol](protocol.md), [manuals](#command-and-interface-manuals) |
| Development and delivery | [Development contract](development.md), [build and test guide](building.md), [release procedures](releases.md), [offline installation](offline-installation.md) |
| Helper Carrier | The current native generator is part of the [Robust architecture](robust-current.md); historical measurements are evidence only and do not imply a supported receiver |
| Simulation channels | [Doppler, delay spread, selective amplitude and FHSS tails](channel-models.md) |
| GPU precision | [GPU precision guidance](gpu-precision-controls.md); bounded RTX PRO 6000 stage measurements are not a qualified full receiver |
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

# Documentation topic index

Start with maintained contracts and current architecture. Historical findings
remain useful only within their stated source, input, configuration and
measurement limits.

Follow the [material lifecycle](development.md#material-lifecycle): distill
useful findings, keep only evidence with a concrete current purpose, and retire
obsolete code, experiments and generated outputs when that purpose ends.

## Current Robust modem

The [current architecture and status](robust-current.md) identifies the
canonical native waveform/channel/Helper stack and the staged Python receiver
boundary. Robust remains unavailable in the GUI pending reconnection. Retained
[pattern research](triad-patterns.md), [stochastic channel findings](triad-stochastic.md),
[rate-aware shaping](triad-shaping.md), and [channel model contract](channel-models.md)
provide bounded technique summaries and evidence limits.

Computationally intensive Robust checks must follow the hierarchical,
computationally minimal statistical method in [the current testing policy](robust-current.md#testing-computationally-intensive-robust-changes).
The [isolated triad regression policy](triad-performance.md) states its
predeclared coverage, uncertainty and provenance requirements. RunPod accelerates
the same method; it does not authorize full or sustained campaigns.

## Other maintained components

| Topic | Start here |
| --- | --- |
| Product and operation | [Project overview](../README.md), [requirements and limits](requirements.md), [protocol](protocol.md), [manuals](#command-and-interface-manuals) |
| Development and delivery | [Development contract](development.md), [build and test guide](building.md), [release procedures](releases.md), [offline installation](offline-installation.md) |
| Helper Carrier | The current native generator is part of the [Robust architecture](robust-current.md); historical measurements are evidence only and do not imply a supported receiver |
| Simulation channels | [Doppler, delay spread, selective amplitude and FHSS tails](channel-models.md) |
| GPU precision | [Historical precision research](gpu-precision-controls.md); no current Robust GPU receiver path is qualified |
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

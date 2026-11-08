# Requirements and implementation coverage

The project includes independent Fast and Legacy modems and retains a Robust GUI
entry. Robust is not currently available through the GUI. Its canonical native
waveform/channel/Helper Generator path is `IndependentApskV2`; Python
`WeakReceiver` acquisition and `PracticalTrackingReceiver` tracking are
sequential stages, not an integrated receive service. See
[current Robust architecture and limits](robust-current.md).

| Component | Current scope | Guidance |
| --- | --- | --- |
| Robust native transmitter/channel | Canonical three-adjacent-carrier APSK waveform, shaping, streaming sampled channel and Helper Generator. No connected end-to-end receiver or GUI session. | [Robust architecture](robust-current.md); concise [pattern](triad-patterns.md), [stochastic channel](triad-stochastic.md), and [shaping](triad-shaping.md) research, with their source-bound limits |
| Fast Modem | Independent text/file-transfer modem with maintained waveform, coding, audio and interface behavior. | [Fast index](indexes/fast.md), [format](fast-mode.md) and linked measured limits |
| Legacy Modem | Independent simplex BPSK31, BPSK125 and Olivia-4/2k text modem. | [Legacy reference](legacy-modem.md) and retained interoperability fixtures |
| Shared interfaces | Toolkit-neutral application mapping, FLTK/Rev adapters, optional terminal/framebuffer/browser frontends and common audio/device services. | [GUI contract](gui-contract.md), [architecture](gui-architecture.md), and [frontend interfaces](frontend-interfaces.md) |
| Received content | Restricted presentation before GUI text, clipboard, draft, or preview; explicit save for retained original payloads. | [Security](security.md) and [receive processing boundaries](receive-processing-hardening.md) |
| Simulation channels | Shared sampled channel behavior where currently connected; software results do not establish physical-device or propagation performance. | [Channel model guide](channel-models.md) |
| Release assurance | Exact published source and binary hashes, supported platform/package scopes, and documented exclusions. | [Release procedures](releases.md) and [current validation](validation.md) |

## Robust testing boundary

Keep one in-tree protocol implementation. Evolve the canonical source when the
wire protocol changes; use Git tags and separate binary releases for older
protocols. Backport a required security fix narrowly to strictly sanitized
dynamic-input surfaces.

Computationally intensive Robust checks use the predeclared hierarchical,
computationally minimal method in [triad-performance.md](triad-performance.md).
Its fixed-geometry kernel estimates do not qualify complete reception or a
physical link. Do not run full, sustained, multi-day, exhaustive, or heavy
calibration campaigns as routine regression or as a fallback. RunPod accelerates
the same method.

# Robust Modem: waveform, receiver and weak signals

[Documentation index](../README.md) · [Security and compatibility](security.md) ·
[Interfaces](interfaces.md) · [Evidence collections](evidence.md)

Robust is also called the regular modem in source and older documents. Explore
the sections relevant to the question; the document's revision and evidence
determine applicability. CPU efficiency, expected SNR, C/N0, synchronization
coverage and decoded-message reliability are related but distinct quantities.

## Implemented waveform and receive behavior

| Document | Context and contents |
| --- | --- |
| [Modem](../modem.md) | Reference: pattern waveform, timing, receiver search and resource limits. |
| [Protocol](../protocol.md) | Reference: fixed-interval wire geometry, exact short/raw paths and physical completion. |
| [Pattern constellation](../pattern-constellation.md) | Reference: binary pattern construction, settling waveform and constellation interpretation. |
| [Development contract](../development.md) | Requirements: short messages, pending progress, physical end and regression preservation. |
| [Modem inspection](../inspection.md) | Reference: flow views, plots and interpretation boundaries. |
| [Packetless stream plan](../packetless-stream-plan.md) | Historical code audit and migration plan for the now-implemented fixed-interval transport. |

## Computation, iterative search and receiver alternatives

| Document | Context and contents |
| --- | --- |
| [Batched iterative search](../search-compute.md) | Implementation reference: FFT acquisition, long-symbol CPU batching, workers, memory boundaries and reproducible workloads. |
| [Pulse-moment qualification](../pulse-moment-validation.md) | Paired raw-PCM performance, conditional sensitivity statistics, numerical equivalence and coverage limits for high-chip iterative search. |
| [Robust CPU costs](../robust-cpu-costs.md) | Paired receive-hardening overhead measurements and estimate coefficients; not a whole-DSP bottleneck profile. |
| [Alternate Robust receiver study](../robust-alternate-receiver.md) | Proposed independent chain: FIR/IIR/FFT tradeoffs, reference reuse, carrier/clock search, CPU efficiency at low expected SNR, GUI/TX isolation and experimental criteria. Source assessment dated 2026-09-30; speedup and sensitivity targets are unmeasured. |
| [Simulation estimates](../simulation-estimates.md) | Reference and models: whole-draft receive probability, reference-machine compute estimates and unsupported coverage. |
| [Receive-processing hardening](../receive-processing-hardening.md) | Implemented mitigations, audit scope and limitations; complements the CPU-cost measurements. |

These documents cover both signal acquisition and post-reception recovery.
Check which layer a finding concerns before applying it to a new optimization.
The alternate-receiver study identifies open profiling and experimental work;
it does not settle which implementation will perform best.

## Low expected SNR, integration and link planning

| Document | Context and contents |
| --- | --- |
| [Weak-signal operation](../weak-signal.md) | Reference and models: integration energy, bandwidth, clock/carrier search, coherence and implemented limits. |
| [Throughput](../throughput.md) | Reference and models: high-SNR chip-count planning, gross throughput and detection-confidence limits. |
| [Link Planner](../link-planner.md) | Controls, command import/export and interpretation of modeled estimates. |
| [Extreme weak-link planning](../weak-link-planning.md) | Statistical reference detectors and `analyze-link` models; separate from production-receiver measurements. |
| [1.2 kHz weak-link case study](../1200hz-weak-link-planning.md) | Modeled 170–230 dB attenuation cases, hypothetical detectors and historical compute limits; recorded runtime estimates predate current detector branches. |
| [Oscillator models](../oscillator-models.md) | Illustrative crystal/GPSDO impairment and coherence assumptions, not hardware measurements. |
| [LPI estimates](../lpi-estimates.md) | Uncalibrated intended-receiver and unkeyed energy-observer models, with applicability limits. |

Supporting measurements, raw samples and reproduction methods are linked in
the documents and the [evidence index](evidence.md#robust-and-receive-processing).

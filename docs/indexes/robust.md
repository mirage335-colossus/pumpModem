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
| [Public alphabet](../public-pattern-alphabet.md) | Local manual checkpoint: independent public amplitude/phase rows, compatibility and measured limits. |
| [Pattern constellation](../pattern-constellation.md) | Reference: binary pattern construction, settling waveform and constellation interpretation. |
| [Development contract](../development.md) | Requirements: short messages, pending progress, physical end and regression preservation. |
| [Modem inspection](../inspection.md) | Reference: flow views, plots and interpretation boundaries. |
| [Clock synchronization](../clock-sync.md) | Local candidate: independent GPS arrival controls, timing evidence, narrow-search prerequisites and hardware limitations. |
| [Spread controls](../spread-spectrum-controls.md) | Local candidate: separate outer DSSS factors, Fake FHSS illustration and unqualified acquisition/regulatory scope. |
| [Helper Carrier](../helper-carrier.md) | Experimental independent tone, optional frequency/phase/dwell-timing assistance, time phase and clear or encrypted callsign metadata, Live/duplex and physical simulation; original-search and hardware qualification limits in the [implementation plan](../helper-carrier-plan.md). |
| [Packetless stream plan](../packetless-stream-plan.md) | Historical code audit and migration plan for the now-implemented fixed-interval transport. |

## Computation, iterative search and receiver alternatives

| Document | Context and contents |
| --- | --- |
| [Batched iterative search](../search-compute.md) | Implementation reference: FFT acquisition, long-symbol CPU batching, workers, memory boundaries and reproducible workloads. |
| [Symbol_Burst](../symbol-burst.md) | Local implementation: whole-symbol UTC scheduling, guarded listening, backlog processing and planner limits; see validation for qualification. |
| [Symbol_Burst implementation handoff](../symbol-burst-implementation-plan.md) | Historical implementation plan: accepted timing decisions, integration stages and validation obligations. |
| [Pulse-moment qualification](../pulse-moment-validation.md) | Paired raw-PCM performance, conditional sensitivity statistics, numerical equivalence and coverage limits for high-chip iterative search. |
| [Transmitter cover waveform](../transmit-cover.md) | Tuning/message common construction, full DSSS surroundings, separate secrets, graceful stop, spectra and qualification limits. |
| [DSSS follow-up evidence](../dsss-followup-validation.md) | Local manual checkpoint: coherent outer-code guard, timing-neighbor completion, duplex, paired regressions and conditional sensitivity limits. |
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
| [Frequency-search cost and amplitude alternatives](../eme-amplitude-receiver.md) | EME/ordinary RF limits, measured clock-cell fix, exact frequency-axis prototype, conditional envelope/segment tradeoffs and CPU-policy prerequisites. |
| [Extreme weak-link planning](../weak-link-planning.md) | Statistical reference detectors and `analyze-link` models; separate from production-receiver measurements. |
| [1.2 kHz weak-link case study](../1200hz-weak-link-planning.md) | Modeled 170–230 dB attenuation cases, hypothetical detectors and historical compute limits; recorded runtime estimates predate current detector branches. |
| [Oscillator models](../oscillator-models.md) | Illustrative crystal/GPSDO impairment and coherence assumptions, not hardware measurements. |
| [LPI estimates](../lpi-estimates.md) | Uncalibrated intended-receiver and unkeyed energy-observer models, with applicability limits. |

Supporting measurements, raw samples and reproduction methods are linked in
the documents and the [evidence index](evidence.md#robust-and-receive-processing).

- [Receiver strategy and CPU budget](../receiver-strategies.md): local complex frequency-axis and amplitude-only implementations, controls and qualification limits.

# Fast Modem: formats, profiles and investigations

[Documentation index](../README.md) · [Security and compatibility](security.md) ·
[Interfaces](interfaces.md) · [Evidence collections](evidence.md#fast-measurements-and-reproduction)

Fast has its own formats and signal paths. Study results retain their original
settings, source revisions, hardware and limitations; earlier defaults and
negative results remain relevant when investigating related behavior.

## Formats and operating controls

| Document | Context and contents |
| --- | --- |
| [Fast mode](../fast-mode.md) | Reference: text/file workflow, profiles, transport and qualification limits. |
| [Capacity codec](../fast-capacity-codec.md) | Reference: fixed coding geometry, LDPC, outer RS and protected source areas. |
| [Capacity integrity](../fast-capacity-integrity.md) | Implementation and analysis: provisional synchronization, integrity-confirmed alignment and false accepts. |
| [XZ source format](../fast-xz.md) | Reference: attachment prefix, compression, resource bounds and post-completion interpretation. |
| [Cycle continuation](../fast-cycle-continuation.md) | Implemented decoding across damaged source cycles, retained holes and diagnostic evidence. |
| [Expected-SNR presets](../fast-snr-presets.md) | Controls, symbol-rate overrides, reference-band models and qualification evidence. |
| [Radio capacity](../fast-radio-capacity.md) | IC-7100 SSB/FM audio profiles, external-radio assumptions and tested limits; not connected-radio qualification. |
| [Acoustic OFDM](../fast-acoustic-ofdm.md) | Reference: geometry, training, tracking, held-out verification and defaults. |
| [Short acoustic transfers](../fast-acoustic-short.md) | Profiles, coding alternatives, preset models and measured/tested limits. |
| [Original Fast mode plan](../fast-mode-plan.md) | Historical isolated APSK design and audit; the implementation subsequently evolved. |

## Coding, cable and radio studies

| Document | Context and contents |
| --- | --- |
| [Coding study](../fast-coding-study.md) | Offline comparisons and historical capacity/coding optimization candidates. |
| [Cable live study](../fast-cable-live-study.md) | Earlier APSK throughput, routing, headroom and small-file reliability evidence. |
| [Capacity live study](../fast-capacity-live-study.md) | QAM/LDPC implementation context and live cable-transfer measurements. |
| [Cable SNR study](../cable-snr-live-study.md) | Measured SNR/SINAD, levels and hardware/measurement-path distinctions. |
| [Cable level calibration](../fast-cable-level-calibration.md) | Waveform-level measurements and a configuration-specific amplitude recommendation. |
| [LDPC rate and margin](../fast-ldpc-rate-margin.md) | Offline production-LDPC comparisons and acoustic-lock limitations; no measured fade-margin claim. |

## Acoustic measurements and failure investigations

| Document | Context and contents |
| --- | --- |
| [Acoustic live study](../fast-acoustic-live-study.md) | Physical-channel measurements and earlier OFDM bulk-transfer experiments. |
| [Acoustic reception diagnosis](../fast-acoustic-reception-diagnosis.md) | Reproduced startup, acquisition and channel-estimation faults and fixes. |
| [Acoustic routing diagnosis](../fast-acoustic-routing-diagnosis.md) | 16/64-QAM and mono/stereo route comparisons, failures and selected defaults. |
| [Acoustic margin targets](../fast-acoustic-margin-targets.md) | Historical noise-margin/throughput models and bounded decoder screening. |
| [Low-SNR recovery](../fast-acoustic-low-snr-recovery.md) | Single-carrier −10 dB premature-end diagnosis, continued-presence handling and checks. |
| [OFDM recovery](../fast-acoustic-ofdm-recovery.md) | Auto 3/0 dB premature-end diagnosis, tracking/interleave changes and reproduction. |

The [evidence index](evidence.md#fast-measurements-and-reproduction) exposes
supporting inventories and method documents, including collections without a
directory README. Physical success, offline replay, modeled probability and
decoder-only measurements have different scopes; retain those distinctions.

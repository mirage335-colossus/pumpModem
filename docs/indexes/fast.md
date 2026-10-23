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

## Historical studies

Completed coding, cable, radio and acoustic investigations are indexed in
[the historical study map](https://github.com/mirage335-colossus/pumpModem/blob/5b343de5ac5a3e33ac404b796c9ade0ba90d9409/docs/indexes/fast.md).
Their raw captures remain in Git. Use them for a specific current question;
physical measurements, offline replay and models have different scopes.

# Security, compatibility and scope

[Documentation index](../README.md) · [Robust](robust.md) · [Fast](fast.md) ·
[Validation evidence](evidence.md)

Use each reference's stated scope and limits. Models, historical reviews and
selected mitigations do not establish universal security or link performance.

| Document | Context and contents |
| --- | --- |
| [Development contract](../development.md) | Required short/raw bits, framing, reception progress, physical completion and regression preservation. |
| [Protocol](../protocol.md) | Implemented wire and receive boundaries. |
| [Cryptography](../crypto.md) | Primitives, keyfiles, stream derivation and mode-specific boundaries. |
| [Security](../security.md) | Current receive/presentation, storage, execution and cryptographic boundaries. |
| [Receive-processing hardening](../receive-processing-hardening.md) | Implemented receive-processing and selected speculative-execution mitigations, audit scope and remaining limits. |
| [Robust CPU costs](../robust-cpu-costs.md) | Paired mitigation-overhead measurements and their limited role in compute estimates. |
| [LPI estimates](../lpi-estimates.md) | Uncalibrated observation models and assumptions; not capability assurance. |
| [Earlier security review](../security-review.md) | Historical packet/APSK findings, superseded by current security/protocol references; retains prior reasoning and limitations. |
| [Requirements matrix](../requirements.md) | Version 001_00 feature coverage and implementation/qualification boundaries. |
| [Version scope](../original-specification.md) | Current scope and remaining automatic RF-tuning capability, despite the historical-sounding filename. |
| [Public-release disclaimer](../disclaimer.md) | Release scope, performance/legal qualifications and license context. |
| [License](../../LICENSE) and [dependency provenance](../../third_party/README.md) | Repository license and retained third-party source/notice information. |

For Fast-specific integrity and source boundaries, see [the Fast format references](fast.md#formats-and-operating-controls).
For runtime host/browser boundaries, see [interfaces](interfaces.md).

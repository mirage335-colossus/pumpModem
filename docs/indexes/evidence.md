# Validation, research evidence and release history

[Documentation index](../README.md) · [Robust studies](robust.md) ·
[Fast studies](fast.md) · [Coordination studies](coordination.md#studies-and-the-reasons-for-later-changes)

These collections preserve measurements, methods, models, negative results,
fixtures and original reports. Dated evidence is not automatically obsolete;
read its source identity, settings, environment and limitations when applying it.
The [validation ledger](../validation.md) records checks and unresolved coverage
across the project. Use topic/heading searches to locate pertinent entries.

Raw [validation data](../validation-data/) is retained in the source repository.
Normal installed documentation omits these captures; use a matching source
checkout or `-DDATAPUMP_INSTALL_VALIDATION_DATA=ON` for an
[offline evidence bundle](../offline-installation.md). Collection inventories
link logs, scripts, samples, source snapshots and hashes; directory links also
expose supporting files without their own Markdown page.

## Robust and receive processing

- [Robust mitigation benchmark inventory and reproduction](../validation-data/robust-mitigations-2026-09-22/README.md), accompanying [CPU-cost interpretation](../robust-cpu-costs.md).
- [Receive-processing results CSV](../validation-data/receive-processing-2026-09-22.csv), with [hardening scope](../receive-processing-hardening.md).
- [Weak-link reference experiments](../weak-link-planning.md) and [1.2 kHz case study](../1200hz-weak-link-planning.md): models and hypothetical detectors with their stated limits.
- [Alternate-receiver proposal](../robust-alternate-receiver.md): unprofiled research directions and proposed comparison criteria.

## Fast measurements and reproduction

The [Fast topic page](fast.md) links the interpretation and diagnosis documents.
This table exposes their supporting inventories and method pages, including
collections that have no root README.

| Collection | Inventories and methods |
| --- | --- |
| [Coding study, 2026-09-19](../validation-data/fast/coding-study-20260919/) | [Inventory](../validation-data/fast/coding-study-20260919/README.md), [LDPC method](../validation-data/fast/coding-study-20260919/ldpc-method.md), [sampled-channel method](../validation-data/fast/coding-study-20260919/sampled-channel-method.md) |
| [Cable live, 2026-09-20](../validation-data/fast/cable-live-20260920/) | [Inventory](../validation-data/fast/cable-live-20260920/README.md), [offline probe](../validation-data/fast/cable-live-20260920/offline-probe-method.md), [reliability model](../validation-data/fast/cable-live-20260920/reliability-model.md) |
| [Cable SNR, 2026-09-20](../validation-data/fast/cable-snr-20260920/) | [Inventory](../validation-data/fast/cable-snr-20260920/README.md) |
| [Capacity, 2026-09-20](../validation-data/fast/capacity-20260920/) | [LDPC/AWGN](../validation-data/fast/capacity-20260920/ldpc-awgn-method.md), [known-symbol QAM](../validation-data/fast/capacity-20260920/qam-known-symbol-method.md), [QAM margin](../validation-data/fast/capacity-20260920/qam-margin-method.md) |
| [Acoustic capacity, 2026-09-20](../validation-data/fast/acoustic-capacity-20260920/) | [Sounder](../validation-data/fast/acoustic-capacity-20260920/sounder-method.md), [BICM](../validation-data/fast/acoustic-capacity-20260920/bicm-method.md), [LDPC half-rate](../validation-data/fast/acoustic-capacity-20260920/ldpc-half-method.md), [two-thirds rate](../validation-data/fast/acoustic-capacity-20260920/ldpc-twothirds-method.md), [receiver diagnostics](../validation-data/fast/acoustic-capacity-20260920/receiver-diagnostics/method.md), [continued experiments](../validation-data/fast/acoustic-capacity-20260920/continued/README.md) |
| [Acoustic reception, 2026-09-21](../validation-data/fast/acoustic-reception-20260921/) | [Inventory](../validation-data/fast/acoustic-reception-20260921/README.md) |
| [Acoustic routing, 2026-09-21](../validation-data/fast/acoustic-routing-20260921/) | [Inventory](../validation-data/fast/acoustic-routing-20260921/README.md) |
| [Acoustic margin targets, 2026-09-21](../validation-data/fast/acoustic-margin-targets-20260921/) | Captured data and scripts; [study interpretation](../fast-acoustic-margin-targets.md) |
| [LDPC rate margin, 2026-09-21](../validation-data/fast/ldpc-rate-margin-20260921/) | Captured data and scripts; [study interpretation](../fast-ldpc-rate-margin.md) |
| [Cable level calibration, 2026-09-21](../validation-data/fast/cable-level-calibration-20260921/) | [Inventory](../validation-data/fast/cable-level-calibration-20260921/README.md), [result summary](../validation-data/fast/cable-level-calibration-20260921/summary.md) |
| [Cycle continuation, 2026-09-21](../validation-data/fast/cycle-continuation-20260921/) | [Inventory](../validation-data/fast/cycle-continuation-20260921/README.md) |
| [Radio capacity, 2026-09-21](../validation-data/fast/radio-capacity-20260921/) | [Inventory](../validation-data/fast/radio-capacity-20260921/README.md) |
| [SNR presets, 2026-09-21](../validation-data/fast/snr-presets-20260921/) | [Inventory](../validation-data/fast/snr-presets-20260921/README.md) |
| [Wire-SNR screening, 2026-09-21](../validation-data/fast/wire-snr-screen-20260921/) | [Inventory](../validation-data/fast/wire-snr-screen-20260921/README.md) |
| [Low rate, 2026-09-21](../validation-data/fast/low-rate-20260921/) | [Inventory](../validation-data/fast/low-rate-20260921/README.md) |
| [Low-SNR acoustic recovery, 2026-09-21](../validation-data/fast/acoustic-low-snr-recovery-20260921/) | [Inventory](../validation-data/fast/acoustic-low-snr-recovery-20260921/README.md), [offline reproduction](../validation-data/fast/acoustic-low-snr-recovery-20260921/offline-reproduction.md) |
| [Acoustic OFDM recovery, 2026-09-21](../validation-data/fast/acoustic-ofdm-recovery-20260921/) | [Inventory](../validation-data/fast/acoustic-ofdm-recovery-20260921/README.md), [results](../validation-data/fast/acoustic-ofdm-recovery-20260921/results.md) |
| [Short acoustic, 2026-09-21](../validation-data/fast/acoustic-short-20260921/) | [Inventory](../validation-data/fast/acoustic-short-20260921/README.md), [LDPC short-frame method](../validation-data/fast/acoustic-short-20260921/ldpc-short-method.md) |
| [Short acoustic, 2026-09-22](../validation-data/fast/acoustic-short-20260922/) | [Inventory](../validation-data/fast/acoustic-short-20260922/README.md) |
| [Short LDPC, 2026-09-22](../validation-data/fast/short-ldpc-20260922/) | [Inventory and independent vectors](../validation-data/fast/short-ldpc-20260922/README.md) |

The [Fast data directory](../validation-data/fast/) also retains standalone
CSV/JSON screens for wire, SSB, FM, acoustic, bulk defaults and earlier sample
rates. Their original files remain available alongside the named collections.

## Independent Legacy fixtures

- [Olivia fixture provenance and software interoperability](../../tests/fixtures/legacy/README.md).
- [BPSK fixture provenance and reproduction](../../tests/fixtures/legacy/PSK-fixtures.md).
- [Legacy modem reference and test limits](../legacy-modem.md).

## Release assurance history

Use the [release guide](../releases.md) for current procedures and the
[validation ledger](../validation.md) for qualification records. The
[30 September 2026 retained-release inventory](../release-history/2026-09-30/README.md)
links original certification attempts and its complete asset manifest, including
warnings, failures, hashes and source/tag identities. Original release notes
remain directly accessible:

- [24 September, 0856 CDT](../release-history/2026-09-30/v001_00-2026-09-24-0856CDT/release-notes.md).
- [26 September, 1847 CDT experiment](../release-history/2026-09-30/v001_00-2026-09-26-1847CDT/release-notes.md).
- [28 September, 1246 CDT](../release-history/2026-09-30/v001_00-2026-09-28-1246CDT/release-notes.md).
- [30 September, 0300 CDT failed draft](../release-history/2026-09-30/v001_00-2026-09-30-0300CDT/release-notes.md).
- [30 September, 0505 CDT](../release-history/2026-09-30/v001_00-2026-09-30-0505CDT/release-notes.md).

The [release-history directory](../release-history/) provides the parent
collection. Historical assurance reports describe their exact releases; they
are not interchangeable with qualification of later source or binaries.

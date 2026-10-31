# Validation and supporting evidence

Use [current validation](../validation.md) for qualified source, run IDs, warnings
and release status. [Latest resource measurements](../ci-resource-results.json)
are one bounded snapshot; Git keeps prior versions. Historical results are not
qualification of new source or binaries.

## Helper Carrier

The [integration guide](../helper-integration.md) maps implemented inexpensive
frequency narrowing, clock/phase correction, complete-dwell buffering and
auxiliary inversion timing to source and focused tests. The
[retained evidence package](../validation-data/helper/README.md) includes selected
per-case results, failures, exact inputs, available provenance and a portable
checker; ignored local logs are not required. Detailed conclusions and older
experiments remain in the [continuation study](../helper-continuation.md) and
its linked studies. Standalone results do not qualify ordinary Robust integration.

## Robust and receive processing

[Retained receiver data](../validation-data/robust/) supports the
[receiver-efficiency prompt](../../FURTHER-agents.txt). The
[Bookworm baseline](../validation-data/bookworm-offline-20260930/) supports the
remaining offline-build qualification. Keep specific useful fixtures and
findings, not whole build histories.

## Fast measurements and reproduction

Completed studies and their raw captures were removed from the current tree.
Their original failures, measurements, scripts and independent vector records
remain in [the pinned historical data](https://github.com/mirage335-colossus/pumpModem/tree/5b343de5ac5a3e33ac404b796c9ade0ba90d9409/docs/validation-data/fast)
and [study index](https://github.com/mirage335-colossus/pumpModem/blob/5b343de5ac5a3e33ac404b796c9ade0ba90d9409/docs/indexes/fast.md).
Retrieve a specific record only when it answers a current question.
[Earlier receiver-cost captures](https://github.com/mirage335-colossus/pumpModem/tree/5b343de5ac5a3e33ac404b796c9ade0ba90d9409/docs/validation-data/robust-mitigations-2026-09-22)
are likewise available in Git.

## Independent Legacy fixtures

[Olivia](../../tests/fixtures/legacy/README.md) and
[BPSK](../../tests/fixtures/legacy/PSK-fixtures.md) inputs remain in the active
regression suite. Their provenance and correctness checks are preserved.

## Release assurance history

[Release history](../release-history/2026-09-30/README.md) preserves published
source and binary identities, failed attempts and certification limits.
Use the [release guide](../releases.md) for current delivery procedures.

## Local historical reports

Ignored `local-evidence/` keeps latest qualification reports, unaccepted patches
and selected fixtures/findings for unfinished work. It is not a general archive
or a reading checklist. Other local paths in dated studies are historical and
may have been retired; use the documented findings and current source.

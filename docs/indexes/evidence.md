# Validation and supporting evidence

Use [current validation](../validation.md) for qualified source, run IDs, warnings
and release status. [Latest resource measurements](../ci-resource-results.json)
are one bounded snapshot; Git keeps prior versions. Historical results are not
qualification of new source or binaries.

## Retained triad research

The [pattern](../validation-data/triad-v3/2026-11-06-independent-patterns/README.md),
[spacing](../validation-data/triad-v3/2026-11-06-spacing/README.md), and
[stochastic-channel](../validation-data/triad-v3/2026-11-05-stochastic/README.md)
records retain selected techniques, source/input provenance, and limitations.
They do not qualify the current receiver or GUI. Archive cleanup is described in
[archive-content-retirement.json](../validation-data/triad-v3/archive-content-retirement.json);
changed container hashes are not new qualification.

## Former Robust and Helper data

The remaining single-carrier Robust and standalone Helper datasets are historical
records only. Their implementation recipes and operating guides are not current
requirements. A result must match its source, configuration, inputs, and
computational regime before it can inform a current decision. It does not qualify
the canonical transmitter/channel/Helper generator or the staged Python receiver
components.

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

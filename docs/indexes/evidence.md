# Validation and supporting evidence

Use [current validation](../validation.md) for qualified source, run IDs, warnings
and release status. [Latest resource measurements](../ci-resource-results.json)
are one bounded snapshot; Git keeps prior versions. Historical results are not
qualification of new source or binaries.

## Retained triad research

The [pattern](../validation-data/triad-v3/2026-11-06-independent-patterns/README.md),
[spacing](../validation-data/triad-v3/2026-11-06-spacing/README.md), and
[stochastic-channel](../validation-data/triad-v3/2026-11-05-stochastic/README.md)
records retain selected techniques, source/input provenance, and limitations
because those findings inform the current triad. They do not qualify the current
receiver or GUI. Retain the concise findings and only the specific source-bound
data needed to interpret them; do not add duplicate captures or new dated
experiment archives by default. Archive cleanup is described in
[archive-content-retirement.json](../validation-data/triad-v3/archive-content-retirement.json);
changed container hashes are not new qualification.

## Former Robust and Helper data

Single-carrier Robust and standalone Helper implementations, guides and routine
performance datasets are retired. Git history may contain their former records;
they are not a reason to restore or retain parallel source, executable copies,
or raw outputs. Preserve only a concise alternative-algorithm conclusion when it
has a concrete bearing on the current triad, with source, configuration, input
and computational-regime limits. Such a finding does not qualify the canonical
native transmitter/channel/Helper generator or complete receiver.

## Fast measurements and reproduction

Fast's former studies and receiver-cost captures are historical Git records, not
a maintained benchmark archive. The [pinned study index](https://github.com/mirage335-colossus/pumpModem/blob/5b343de5ac5a3e33ac404b796c9ade0ba90d9409/docs/indexes/fast.md)
summarizes relevant conclusions; retrieve a specific [measurement](https://github.com/mirage335-colossus/pumpModem/tree/5b343de5ac5a3e33ac404b796c9ade0ba90d9409/docs/validation-data/fast)
or [receiver-cost capture](https://github.com/mirage335-colossus/pumpModem/tree/5b343de5ac5a3e33ac404b796c9ade0ba90d9409/docs/validation-data/robust-mitigations-2026-09-22)
only when it answers a current question. Current Fast behavior remains covered
by its active regression suite and guidance.

## Independent Legacy fixtures

[Olivia](../../tests/fixtures/legacy/README.md) and
[BPSK](../../tests/fixtures/legacy/PSK-fixtures.md) inputs remain in the active
regression suite. Their provenance and correctness checks are preserved.

## Release assurance history

[Release history](../release-history/2026-09-30/README.md) preserves published
source and binary identities, failed attempts and certification limits.
Use the [release guide](../releases.md) for current delivery procedures.

## Local historical reports

Ignored `local-evidence/` is temporary working space, not a historical archive.
Each retained item must support a current question or active unfinished task,
with a stated cleanup trigger. Keep only compact source-bound evidence
needed for the decision; move concise reusable findings, including negative
results and assumptions, into maintained docs, then remove redundant raw data,
superseded reports and abandoned experiment outputs. Preserve published assets
under the release contract and original failure evidence while certification or
an unresolved diagnosis requires it. Git history is the source for superseded
tracked documentation; it is not a reason to keep obsolete guides in the tree.

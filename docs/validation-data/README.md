# Retained development inputs

This directory keeps selected receiver evidence and the Bookworm offline-build
baseline. Active regression fixtures live under `tests/`. The measurements and
their recorded commands are historical provenance, not current execution
instructions or qualification. The current
[abbreviated testing policy](../triad-performance.md) supersedes old campaign
recipes, including preservation and reproduction instructions inside dated
records. Those records do not grant an exemption from the current
[material lifecycle](../development.md#material-lifecycle): keep underlying
data only for a concrete current purpose that a concise finding cannot serve,
and retire redundant material when that purpose ends. Provenance completeness
is stated by each record.

The [triad archive-retirement manifest](triad-v3/archive-content-retirement.json)
records removed archived source/build/test drivers and checksum updates.
Retained data leaves were verified byte-identical; outer archive and checksum
values changed where source payloads were removed. Original source/internal
hashes and recorded commands remain historical provenance. Changed container
hashes do not represent new qualification, and retained measurements do not
validate the current receiver or GUI. Original hashes remain in each record or
the retirement manifest; do not use an old container hash as the hash of its
repacked replacement.

[Helper Carrier evidence](helper/README.md) preserves selected standalone
frequency, phase, timing and complete-stream results, including failed cases
and public fixture keys. The current Helper Generator is part of the native
stack described in [Robust architecture](../robust-current.md); archived
receiver experiments are not supported operation guidance.

Completed Fast studies and other superseded captures are available in
[Git history](https://github.com/mirage335-colossus/pumpModem/tree/5b343de5ac5a3e33ac404b796c9ade0ba90d9409/docs/validation-data), rather than copied or
compressed again here. See [current validation](../validation.md),
[future prompts](../../FURTHER-agents.txt) and [evidence navigation](../indexes/evidence.md).

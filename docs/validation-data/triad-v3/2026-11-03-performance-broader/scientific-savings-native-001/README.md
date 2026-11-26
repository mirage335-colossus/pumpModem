# Stopped native savings experiment 001

The frozen three-block experiment stopped after the second block because the
second abbreviated arm did not establish its model guard. The result is
**incomparable**, with no three-block cost interval or savings claim.

| Completed block | Arm order | Abbreviated wall | Direct wall | Outcome |
| --- | --- | ---: | ---: | --- |
| 1 | Abbreviated, direct | 57.819093 s | 46.949392 s | Both configured checks passed. |
| 2 | Direct, abbreviated | 57.155739 s | 46.079414 s | Abbreviated guard inconclusive; experiment stopped. |

Run wall time was 210.621937 s. It includes 1.981608 s of initial setup and full
arm subprocess startup/finalization. It excludes outer interpreter startup,
argument parsing, final summary serialization, and console output. The separate
freeze receipt records 1.703296 s; it is not added to the reported run wall time.
Shared controller/freeze costs and prior qualification cost retain the treatment
specified by the frozen plan. The observed arm times do not establish positive
comparable savings.

`preservation.json` indexes the exact plan, freeze/setup receipts, four arms,
two forecasts, logs, historical anchor, and native build provenance. Compact JSON
gzip payloads reconstruct each original file with its original indentation and
newline; original byte counts and SHA256 digests are checked. The plan's semantic
digest starts `fda3d09f`; its distinct file digest starts `5ef04c05`.

The original `matching-source.tar.gz` (SHA256
`d6bb1f14f7f29eaadf8da560f70a0354a76f11357c03b391a56b628b57c8a833`) contained
the exact experiment script (`d8a594ea`) and the five helper/config files
identified by its plan. Its current sanitized replacement retains one member;
five source members were removed. The
[retirement manifest](../../archive-content-retirement.json) records the
current archive SHA256 as
`00a59ac140aa25771162ab492adcebbce5b8cda4d92c556f69f621b9f9ebc71d` and lists
the removed files. No later direct-route implementation or active GPU result is
included. Native executables are omitted; their hashes, build provenance,
arguments, input seeds, and historical anchor remain in the records.
`reproduction.json` is a derived recipe, explicitly not a retained historical
outer command.

This independent append leaves the earlier scientific bundle unchanged. No
benchmark was rerun to create it. The historical `python3 verify.py` command
is not available in the current checkout; `SHA256SUMS` and the preservation
records retain the archive's integrity evidence.
The README checksum in `SHA256SUMS` predates these status clarifications.

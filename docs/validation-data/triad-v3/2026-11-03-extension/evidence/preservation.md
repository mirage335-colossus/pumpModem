# Reusing the preserved experiment outputs

The complete experiment tree was retained before disposal of the temporary
compute instance. Its immutable compressed archive is available locally at
`local-evidence/triad-extension/remote/triad-extension-evidence.tar.zst`.
The adjacent `evidence-inventory.json` lists every payload path, size and SHA256.
These large files are ignored evidence, not repository dependencies or routine
regression inputs. Keep the compact maintained reports when moving the source
tree; explicitly arrange transfer of this archive when raw observations are needed.

The [archive metadata](archive-metadata.json) and
[independent local verification](archive-verification.json) record:

| Item | Verified amount |
| --- | ---: |
| Compressed archive | 7,168,684,665 bytes |
| Payload files | 8,635 |
| Logical payload bytes | 42,940,313,616 |
| Unique regular payload bytes | 28,532,930,047 |
| Hardlink aliases to earlier identical content | 3,199 |

Archive SHA256:
`fcedba8e0c40d48a3e06fed83250552619f8e992f125353c850d092de41702cf`.
The remote packer checked 4,952 output bindings from 56 stage receipts against
actual bytes. The independent local pass verified the compressed checksum,
external and embedded inventory, every regular payload and each hardlink's
previously verified target. It found no missing, duplicate or unexpected
payloads. It did not repeat the receipt-to-output semantic audit. The pass took
92.50 seconds without extracting any payload; its decompressor exited and was
reaped successfully.

The archive includes original and repaired score outputs, failed-job receipts,
all retained raw grouped statistics, both native binaries and build proofs,
source packages and overlays, sampled phase workspaces, the projection
diagnostic and exporter parity/benchmark data. Those distinct source versions
are intentional. Do not describe the original campaigns as rerun with the
later paired-export optimization, or replace an original source binding with
the hash of the current working tree.

For a receiver-only comparison, restore the appropriate observations and verify
the scorer's public input hashes. The compact packages under the same local
`remote/` directory are sufficient for the documented score recounts but omit
the large raw arrays. The separate physical and phase recipes explain their
different evidence levels and model assumptions.

To repeat the bounded, read-only archive check without extraction:

```sh
EVIDENCE=local-evidence/triad-extension/remote
python3 docs/validation-data/triad-v3/2026-11-03-extension/evidence/verify-archive.py \
  --archive "$EVIDENCE/triad-extension-evidence.tar.zst" \
  --inventory "$EVIDENCE/evidence-inventory.json" \
  --metadata "$EVIDENCE/triad-extension-evidence.archive.json" \
  --output "$EVIDENCE/archive-verification-repeat.json" --max-seconds 900
```

The output must be fresh. A POSIX host (tested on Linux), Python and the `zstd`
executable are required; the verifier uses POSIX signals and process groups. The
[verifier](verify-archive.py) uses bounded payload reads and rejects unsafe
paths, unsupported member types, inconsistent links and extra payloads. Its
[seven synthetic checks](archive-verifier-selftest.json) passed. The preserved
[packer](pack-archive.py) documents the original fixed input selection; its
hard-coded experiment paths are not a generic cloud-management interface.

If extracting the full archive, use a fresh ignored directory with space for at
least 29 GB of unique payload plus filesystem overhead, and preserve hardlinks.
Treat all extracted evidence as immutable: editing one hardlinked file changes
its aliases. A selectively extracted alias also requires its earlier target;
prefer complete extraction or resolve dependencies from the verified archive.
Do not make either full extraction or these long experiments part of ordinary
receiver regression testing.

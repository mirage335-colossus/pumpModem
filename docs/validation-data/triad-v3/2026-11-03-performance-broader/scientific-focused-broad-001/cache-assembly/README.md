# Reusable GPU fixture cache

The assembled, ignored cache is
`local-evidence/triad-performance-broader/gpu/reusable-fixtures`.
All 20 `PUBLIC_FILES` were verified: 109,435,833 bytes across five cases.
Fifteen binary files are hardlinks to retained original arrays; ten metadata
files are exact copies. Original file contents remain unchanged. There was no
regeneration, receiver execution, or new timing measurement.

| Case | Metadata selection | Input identity binding |
| --- | --- | --- |
| broad-hf-timing | Original qualified cache | Original qualified GPU FFT anchor |
| free-crystal | Common cache | New strict-identical GPU anchor |
| hf30-fhss-missed | Original qualified cache | Original physical fixture receipt |
| narrow-hf-tight | Original qualified cache | Original physical fixture receipt |
| vlf-850-day | Original qualified cache | Original physical fixture receipt |

The common cache retained only broad/free metadata locally, so it could not be
used alone. Only free-crystal's `fixture.json` and `workspace/manifest.json`
were selected from it. The free binary hashes, physical identity, source hashes,
and cache key equal the original free fixture's values. The common broad
manifest was deliberately excluded because the focused broad check uses the
original qualified manifest.

`receipt.json.gz` preserves exact source paths, sizes, SHA-256 hashes, selected
model identities, assembly steps, and original-source verification. Its raw SHA
is `561e4ddaa84c7560c9d18c2506131d11dfe3d51af473d2855d3c53df113d103d`.
`metadata-and-anchors.tar.gz` retains all ten selected metadata files and both
applicable anchor records, indexed by `input-index.json`. The parent verifier
checks this portable metadata closure. Physical arrays remain outside the
maintained archive; their actual byte verification is the recorded assembly
operation, not a claim that a future checkout includes them.

For a future GPU wrapper invocation, use this cache with
`--fixture-cache local-evidence/triad-performance-broader/gpu/reusable-fixtures`
and `--identical-fft-case free-crystal`. The directory supplied through
`--identical-anchor-directory` must contain the exact retained
`gpu/final-routine/blackwell-identical-cost-001/same-hardware-anchor.json`
bytes under the receiver's expected name `free-crystal.identical-anchor.json`.
For broad-HF, keep the original qualified GPU legacy anchor
`1793764120402602191-700ad521a6ec37f0.anchor.json` from
`gpu/legacy/legacy-anchors/cuda/`. The receipt binds both anchor hashes. This
mapping does not waive any other source, runtime, device, precision, coverage,
or stability guard and does not establish a complete routine outcome.

The following file-only procedure can reconstruct a new cache from retained
local inputs. Run from the repository root, choose a destination that does not
exist, and keep hardlinked arrays immutable. It verifies every source before
creating the target, copies metadata, links binary files, and checks every
assembled file. It does not import or run receiver code.

```sh
taskset -c 6,8 python3 - <<'PY'
import gzip, hashlib, json, os
from pathlib import Path
archive = Path('docs/validation-data/triad-v3/2026-11-03-performance-broader/scientific-focused-broad-001/cache-assembly')
receipt = json.loads(gzip.decompress((archive / 'receipt.json.gz').read_bytes()))
target = Path('local-evidence/triad-performance-broader/gpu/reusable-fixtures-copy')
assert not target.exists()
def valid(path, row):
    return path.stat().st_size == row['bytes'] and hashlib.sha256(path.read_bytes()).hexdigest() == row['sha256']
assert all(valid(Path(row['source_path']), row) for row in receipt['files'])
for row in receipt['files']:
    source, output = Path(row['source_path']), target / row['path']
    output.parent.mkdir(parents=True, exist_ok=True)
    if row['method'] == 'hardlink':
        os.link(source, output)
    else:
        output.write_bytes(source.read_bytes())
assert all(valid(target / row['path'], row) for row in receipt['files'])
assert all(valid(Path(row['source_path']), row) for row in receipt['files'])
print('Verified five fixture receipts and 20 public files; no receivers executed.')
PY
```

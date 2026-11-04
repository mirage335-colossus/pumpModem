#!/usr/bin/env python3
"""Verify archived bytes without executing any receiver workload."""
import gzip
import hashlib
import io
import json
from pathlib import Path
import tarfile

root = Path(__file__).resolve().parent
receipt = json.loads((root / "preservation.json").read_text())
digest = lambda value: hashlib.sha256(value).hexdigest()
for row in receipt["records"]:
    packed = (root / row["path"]).read_bytes()
    assert digest(packed) == row["compressed_sha256"], row["path"]
    raw = gzip.decompress(packed)
    assert len(raw) == row["bytes"] and digest(raw) == row["sha256"], row["path"]
snapshot = receipt["source_snapshot"]
packed = (root / snapshot["path"]).read_bytes()
assert digest(packed) == snapshot["sha256"]
summary = json.loads(gzip.decompress((root / "cpu-complete-001/summary.json.gz").read_bytes()))
with tarfile.open(fileobj=io.BytesIO(packed), mode="r:gz") as archive:
    assert len(archive.getmembers()) == snapshot["members"]
    for name, expected in {**summary["source_sha256"], **summary["protocol_sha256"]}.items():
        assert digest(archive.extractfile(name).read()) == expected, name
assert summary["status"] != "running"
print(f"Verified {len(receipt['records'])} raw records and {snapshot['members']} source members; no workloads executed.")

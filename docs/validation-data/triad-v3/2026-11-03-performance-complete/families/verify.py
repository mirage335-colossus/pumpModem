#!/usr/bin/env python3
"""Verify preserved bytes, reconstruct embedded receipts, and check sources.

No receiver or simulator executes. Run from any working directory.
"""
import gzip
import hashlib
import io
import json
from pathlib import Path
import tarfile


ROOT = Path(__file__).resolve().parent


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    for line in (ROOT / "SHA256SUMS").read_text().splitlines():
        expected, name = line.split("  ", 1)
        assert sha((ROOT / name).read_bytes()) == expected, name
    preservation = json.loads((ROOT / "preservation.json").read_bytes())
    documents = {}
    for row in preservation["archives"]:
        path = ROOT / row["archive"]
        encoded = path.read_bytes()
        assert sha(encoded) == row["gzip_sha256"], path
        assert len(encoded) == row["gzip_bytes"], path
        data = gzip.decompress(encoded)
        assert len(data) == row["bytes"] and sha(data) == row["sha256"], path
        if path.name.endswith(".json.gz"):
            documents[row["archive"]] = json.loads(data)
    for row in preservation["embedded_receipts"]:
        value = documents[row["archive"]]
        for key in row["json_pointer"].split("/")[1:]:
            value = value[int(key)] if isinstance(value, list) else value[key]
        data = (json.dumps(value, indent=2, allow_nan=False) + "\n").encode()
        assert len(data) == row["bytes"] and sha(data) == row["sha256"], row["source"]
    for row in preservation.get("source_aliases", []):
        data = gzip.decompress((ROOT / row["archive"]).read_bytes())
        assert len(data) == row["bytes"] and sha(data) == row["sha256"], row["source"]
    source_count = 0
    source_indexes = sorted(ROOT.glob("source-index*.json"))
    for index_path in source_indexes:
        index = json.loads(index_path.read_bytes())
        sources = index["files"]
        source_count += len(sources)
        source_archive = index.get("source_archive", "matching-python-source.tar.gz")
        with tarfile.open(fileobj=io.BytesIO(gzip.decompress(
                (ROOT / source_archive).read_bytes())), mode="r:") as archive:
            assert {m.name for m in archive.getmembers()} == {r["member"] for r in sources}
            for row in sources:
                data = archive.extractfile(row["member"]).read()
                assert sha(data) == row["sha256"] and len(data) == row["bytes"], row["member"]
                name = Path(row["member"]).name
                for result_path in row["required_by"]:
                    identity = documents[result_path + ".gz"]["identity"]
                    hashes = [identity.get("measurement_dependencies", {}).get(name)]
                    for kind in ("adapters", "source_sha256"):
                        hashes.extend(values.get(name) for values in identity.get(kind, {}).values()
                                      if isinstance(values, dict))
                    assert row["sha256"] in hashes, (result_path, name)
    provenance = json.loads((ROOT / "reference-build-provenance.json").read_bytes())
    binaries = {provenance[key]["sha256"] for key in ("reference_binary", "current_binary")}
    for name, document in documents.items():
        if name.endswith("/result.json.gz") and "identity" in document:
            assert set(document["identity"]["binaries"].values()) <= binaries, name
    plan = documents["crossed-diagnostic/cpu-cross-001/plan.json.gz"]
    script = gzip.decompress((ROOT / "crossed-diagnostic/crossed.py.gz").read_bytes())
    assert sha(script) == plan["script_sha256"]
    assert documents["crossed-diagnostic/cpu-cross-001/result.json.gz"]["plan_sha256"] == plan["plan_sha256"]
    assert plan["source_report_sha256"] in {row["sha256"] for row in preservation["archives"]}
    print(f"Verified {len(preservation['archives'])} exact gzip records, "
          f"{len(preservation['embedded_receipts'])} byte-exact embedded receipts, "
          f"{source_count} matching source members across {len(source_indexes)} snapshots "
          "and native binary provenance identities.")


if __name__ == "__main__":
    main()

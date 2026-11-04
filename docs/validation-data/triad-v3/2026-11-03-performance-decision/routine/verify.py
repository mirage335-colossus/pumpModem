#!/usr/bin/env python3
"""Verify this archive without running a receiver, test suite, or benchmark."""
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
        expected, relative = line.split("  ", 1)
        assert sha((ROOT / relative).read_bytes()) == expected, relative
    receipt = json.loads((ROOT / "preservation.json").read_bytes())
    documents = {}
    for row in receipt["records"]:
        encoded = (ROOT / row["archive"]).read_bytes()
        assert sha(encoded) == row["gzip_sha256"] and len(encoded) == row["gzip_bytes"], row["archive"]
        assert encoded[3] == 0 and encoded[4:8] == bytes(4), row["archive"]
        data = gzip.decompress(encoded)
        assert sha(data) == row["sha256"] and len(data) == row["bytes"], row["original"]
        if row["archive"].endswith(".json.gz"):
            documents[row["archive"]] = json.loads(data)
    summary = documents["cpu-decision-001/summary.json.gz"]
    assert summary["status"] != "running"
    expected_sources = {**summary["source_sha256"], **summary["protocol_sha256"]}
    with tarfile.open(fileobj=io.BytesIO(gzip.decompress(
            (ROOT / "matching-source-and-tests.tar.gz").read_bytes())), mode="r:") as archive:
        assert {m.name for m in archive.getmembers()} == {r["member"] for r in receipt["sources"]}
        for row in receipt["sources"]:
            member = archive.getmember(row["member"])
            assert member.mtime == member.uid == member.gid == 0
            data = archive.extractfile(member).read()
            assert sha(data) == row["sha256"] and len(data) == row["bytes"], row["member"]
            if row["routine_recorded"]:
                assert expected_sources[row["member"]] == row["sha256"], row["member"]
    assert receipt["coverage_references"] == summary["coverage_receipts"]
    projection = documents["identical-anchor-inputs/reporting-projection.json.gz"]
    historical_source = ROOT / "../../2026-11-03-performance-complete/identical-fft/matching-source.tar.gz"
    assert sha(historical_source.read_bytes()) == projection["source_archive_sha256"]
    archived_sources = {row["member"]: row["sha256"] for row in receipt["sources"]}
    for name, pair in projection["measurement_files"].items():
        assert archived_sources["tools/" + name] == pair["current_sha256"]
    for row in receipt["coverage_reference_checks"]:
        assert sha((ROOT / row["maintained_relative"]).read_bytes()) == row["sha256"]
        assert row["original_and_maintained_bytes_matched"] and not row["fresh_coverage_run"]
    provenance = json.loads((ROOT / "reference-build-provenance.json").read_bytes())
    assert summary["native_binary_sha256"]["candidate"] == provenance["current_binary"]["sha256"]
    assert summary["native_binary_sha256"]["reference"] == provenance["reference_binary"]["sha256"]
    assert len(summary["timing_cases"]["rows"]) == 23
    assert all(row["gate"] == "pass" for row in summary["timing_cases"]["rows"])
    assert summary["exit_code"] == 2 and summary["status"] == "incomplete" and summary["errors"] == []
    print(f"Verified {len(receipt['records'])} exact compressed records, "
          f"{len(receipt['sources'])} source/test members and unchanged coverage-reference metadata. "
          "Maintained coverage payload hashes match; executable binaries are referenced, not included.")


if __name__ == "__main__":
    main()

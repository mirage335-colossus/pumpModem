#!/usr/bin/env python3
"""Verify exact preserved evidence; optionally replay archived model proof only."""
import argparse
import gzip
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parent


def sha(value):
    return hashlib.sha256(value).hexdigest()


def digest(value):
    return sha(json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--replay-model", action="store_true")
    parser.add_argument("--model-case", choices=("cpu-free-001", "gpu-free-002"), action="append",
                        help="require this anchor's exact arithmetic environment and replay")
    args = parser.parse_args()
    for line in (ROOT/"SHA256SUMS").read_text().splitlines():
        expected, name = line.split("  ", 1)
        assert sha((ROOT/name).read_bytes()) == expected, name
    preservation = json.loads((ROOT/"preservation.json").read_text())
    documents = {}
    for row in preservation["archives"]:
        encoded = (ROOT/row["archive"]).read_bytes()
        assert len(encoded) == row["gzip_bytes"] and sha(encoded) == row["gzip_sha256"]
        raw = gzip.decompress(encoded)
        assert len(raw) == row["bytes"] and sha(raw) == row["sha256"]
        if row["archive"].endswith(".json.gz"):
            documents[row["archive"]] = json.loads(raw)
    for row in preservation["duplicate_artifacts"]:
        raw = gzip.decompress((ROOT/row["archive"]).read_bytes())
        assert len(raw) == row["bytes"] and sha(raw) == row["sha256"]
    records = {name: documents[name+"/free-crystal/result.json.gz"]
               for name in ("cpu-free-001", "gpu-free-002")}
    for name, record in records.items():
        assert record["record_sha256"] == digest({k: v for k, v in record.items() if k != "record_sha256"})
        assert record["identity_sha256"] == digest(record["identity"])
        assert record["identity_agreement"]["identity"] == record["identity"]
        assert record["identity_agreement"]["status"] == "implementation_unchanged"
        assert not record["identity_agreement"]["different"] and not record["identity_agreement"]["missing"]
        assert record["frozen"] == documents[name+"/free-crystal/forecast.json.gz"]
        assert record["frozen"]["commitment_sha256"] == digest({k: v for k, v in record["frozen"].items() if k != "commitment_sha256"})
        lags = record["identity"]["geometry"]["lags"]
        for row in record["training"]+record["full_runs"]:
            for score in row["scores"]:
                work = score["work"]
                assert sum(work["rank_histogram"]) == score["count"]*lags
                assert work["measured_group_count"] == work["completed_group_count_from_rank_histogram"] == score["count"]*lags
    sources = json.loads((ROOT/"source-index.json").read_text())["files"]
    encoded = (ROOT/preservation["source_archive"]).read_bytes()
    assert sha(encoded) == preservation["source_archive_sha256"]
    with tarfile.open(fileobj=io.BytesIO(gzip.decompress(encoded)), mode="r:") as archive:
        assert {m.name for m in archive.getmembers()} == {row["member"] for row in sources}
        contents = {}
        for row in sources:
            data = archive.extractfile(row["member"]).read()
            assert len(data) == row["bytes"] and sha(data) == row["sha256"]
            contents[row["member"]] = data
            for required in row["required_by"]:
                runtime = documents[required]["identity"]["runtime"]
                expected = runtime["repository_modules"].get(row["member"],
                    runtime["measurement_files"].get(Path(row["member"]).name))
                assert expected == row["sha256"]
        for record in records.values():
            runtime = record["identity"]["runtime"]
            required = {*runtime["repository_modules"], *("tools/"+name for name in runtime["measurement_files"])}
            assert required <= contents.keys()
    replayed, unavailable = [], {}
    if args.replay_model or args.model_case:
        import numpy
        import scipy
        versions = {"python": sys.version, "numpy": numpy.__version__, "scipy": scipy.__version__}
        with tempfile.TemporaryDirectory(prefix="triad-fft-proof-") as directory:
            temporary = Path(directory)
            for member, data in contents.items():
                destination = temporary/member
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(data)
            sys.path.insert(0, str(temporary/"tools"))
            spec = importlib.util.spec_from_file_location("archived_identical_fft", temporary/"tools/triad_perf_identical_fft.py")
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            for name, record in records.items():
                if args.model_case and name not in args.model_case:
                    continue
                recorded = record["identity"]["runtime"]["versions"]
                different = {key: {"actual": value, "recorded": recorded[key]}
                             for key, value in versions.items() if value != recorded[key]}
                if different:
                    unavailable[name] = different
                    assert not args.model_case, f"{name}: recorded arithmetic environment unavailable: {different}"
                    continue
                assert module.anchor_valid(record), name
                replayed.append(name)
    print(f"Verified {len(preservation['archives'])} exact gzip records, {len(sources)} indexed source snapshots, "
          f"and two source/forecast/work receipts.")
    print(json.dumps({"model_replay_passed": replayed, "model_replay_unavailable_environment": unavailable,
                      "model_replay_requested": bool(args.replay_model or args.model_case)}, indent=2))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Verify retained workspace files against receiver/convergence input_hashes.

Usage: python verify_retained_arrays.py --input EVIDENCE_ROOT --output RESULT.json
Standard library only. A missing array is recorded as omitted, never verified.
Conflicting report expectations, missing manifests, or retained-file mismatches
fail before a new result is written. Each distinct workspace/file is hashed once.
"""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys


SCHEMAS = {
    "triad-static-frequency-study-v1", "triad-rbpf-study-v1",
    "triad-noise-study-v1", "triad-rbpf-convergence-v1",
}
ANCHORS = {"sub9-screen", "sub9-holdout", "month-pilot"}
OUTPUT_SCHEMA = "triad-retained-array-hashes-v1"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def read_json(path):
    def invalid(value):
        raise ValueError(f"nonfinite JSON constant in {path.name}: {value}")
    return json.loads(path.read_text(), parse_constant=invalid)


def anchored_identity(path):
    parts = Path(path).parts
    for index, part in enumerate(parts):
        if part in ANCHORS:
            return "/".join(parts[index:])
    return None


def resolve_workspace(source_path, manifests):
    anchored = anchored_identity(source_path)
    if anchored is not None:
        require(anchored in manifests, f"manifest not found for {source_path}")
        return anchored
    parts = Path(source_path).parts
    matches = [key for key in manifests if parts[-len(Path(key).parts):] == Path(key).parts]
    require(len(matches) == 1,
            f"expected one relocated workspace for {source_path}; found {len(matches)}")
    return matches[0]


def filename(value):
    require(isinstance(value, str) and value not in ("", ".", "..")
            and Path(value).name == value and not Path(value).is_absolute(),
            f"invalid workspace filename: {value!r}")
    return value


def hash_nodes(value, location=()):
    if isinstance(value, dict):
        if "input_hashes" in value:
            yield location, value
        for key, child in value.items():
            yield from hash_nodes(child, location + (str(key),))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from hash_nodes(child, location + (str(index),))


def verify(root):
    root = root.resolve()
    relative = lambda path: path.relative_to(root).as_posix()
    manifests = {}
    for path in sorted(root.rglob("manifest.json")):
        doc = read_json(path)
        if not isinstance(doc, dict) or doc.get("format") != "triad-block-stats-v1":
            continue
        key = anchored_identity(path.parent) or relative(path.parent)
        require(key not in manifests, f"duplicate workspace identity {key}")
        manifests[key] = (path, doc)
    require(manifests, "no sampled workspace manifests found")

    expected = {}
    references = Counter()
    first_source = {}
    source_reports = []
    unhashed_capture_records = []
    for report in sorted(root.rglob("*.json")):
        if report.name == "manifest.json":
            continue
        doc = read_json(report)
        if not isinstance(doc, dict) or doc.get("schema") not in SCHEMAS:
            continue
        report_name = relative(report)
        nodes = list(hash_nodes(doc))
        for location, node in nodes:
            require(isinstance(node.get("input"), str),
                    f"input_hashes lacks its workspace input in {report_name}:{'/'.join(location)}")
            workspace = resolve_workspace(node["input"], manifests)
            require(isinstance(node["input_hashes"], dict) and node["input_hashes"],
                    f"empty or invalid input_hashes in {report_name}:{'/'.join(location)}")
            for name, sha in node["input_hashes"].items():
                filename(name)
                require(isinstance(sha, str) and re.fullmatch(r"[0-9a-f]{64}", sha),
                        f"invalid expected SHA256 in {report_name}: {workspace}/{name}")
                key = (workspace, name)
                if key in expected:
                    require(expected[key] == sha,
                            f"conflicting expected hashes for {workspace}/{name}: "
                            f"{first_source[key]} versus {report_name}")
                else:
                    expected[key] = sha
                    first_source[key] = report_name
                references[key] += 1
        for index, capture in enumerate(doc.get("captures", [])):
            if "input_hashes" not in capture:
                unhashed_capture_records.append({"report": report_name, "capture_index": index,
                                                "input": resolve_workspace(capture["input"], manifests),
                                                "status": capture.get("status"),
                                                "verified": False})
        source_reports.append({"report": report_name, "schema": doc["schema"],
                               "sha256": digest(report), "input_hash_records": len(nodes)})
    require(expected, "no receiver/convergence input_hashes references found")

    workspaces = {}
    summary = Counter()
    for workspace, (manifest_path, manifest) in sorted(manifests.items()):
        verified, omitted = {}, []
        requested = sorted(name for key, name in expected if key == workspace)
        for name in requested:
            path = manifest_path.parent / name
            require(path.resolve().is_relative_to(root), f"file escapes evidence root: {workspace}/{name}")
            if not path.exists():
                require(name != "manifest.json", f"missing referenced manifest: {workspace}")
                omitted.append(name)
                summary["omitted_referenced_files"] += 1
                continue
            require(path.is_file(), f"referenced path is not a file: {workspace}/{name}")
            actual = digest(path)
            require(actual == expected[(workspace, name)],
                    f"retained file hash mismatch: {workspace}/{name}; "
                    f"expected {expected[(workspace, name)]}, actual {actual}")
            size = path.stat().st_size
            verified[name] = {"sha256": actual, "bytes": size,
                              "hash_references": references[(workspace, name)]}
            summary["verified_files"] += 1
            summary["verified_bytes"] += size
            summary["verified_manifests" if name == "manifest.json" else "verified_arrays"] += 1
        declared = {"manifest.json"}
        for key, value in manifest.items():
            if key.endswith("_file"):
                declared.add(filename(value))
        unreferenced = sorted(name for name in declared
                              if name not in requested and (manifest_path.parent / name).is_file())
        summary["retained_declared_files_without_report_hash"] += len(unreferenced)
        summary["workspaces_without_hash_references"] += int(not requested)
        workspaces[workspace] = {"verified_files": verified, "omitted_referenced_files": omitted,
                                 "retained_declared_files_without_report_hash": unreferenced}
    summary.update({"workspace_manifests_found": len(manifests),
                    "source_reports": len(source_reports),
                    "input_hash_records": sum(r["input_hash_records"] for r in source_reports),
                    "unique_referenced_files": len(expected),
                    "file_hash_references": sum(references.values()),
                    "files_with_multiple_hash_references": sum(n > 1 for n in references.values()),
                    "capture_records_without_input_hashes": len(unhashed_capture_records)})
    require(summary["verified_files"] + summary["omitted_referenced_files"] == len(expected),
            "internal referenced-file accounting mismatch")
    return {"schema": OUTPUT_SCHEMA, "verifier_sha256": digest(Path(__file__)),
            "scope": "Independent SHA256 recomputation of retained files referenced by receiver/convergence input_hashes. Each unique workspace/file is hashed once after all expected hashes agree. Missing arrays and retained files without a report hash are not verified. Hash agreement is not a DSP correctness or physical-channel validation.",
            "summary": dict(summary), "hash_conflicts": 0, "retained_hash_mismatches": 0,
            "workspaces": workspaces, "source_reports": source_reports,
            "capture_records_without_input_hashes": unhashed_capture_records}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    require(args.input.is_dir(), "input must be an evidence directory")
    if args.output.exists():
        old = read_json(args.output)
        require(isinstance(old, dict) and old.get("schema") == OUTPUT_SCHEMA,
                "output must not overwrite another artifact")
    result = verify(args.input)
    encoded = json.dumps(result, separators=(",", ":"), sort_keys=True, allow_nan=False) + "\n"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(encoded)
    print(json.dumps({"output": str(args.output), "bytes": len(encoded.encode()), **result["summary"]}))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError) as error:
        print(f"retained-array verification failed: {error}", file=sys.stderr)
        sys.exit(1)

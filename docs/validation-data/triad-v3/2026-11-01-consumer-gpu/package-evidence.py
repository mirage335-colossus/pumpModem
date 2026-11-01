#!/usr/bin/env python3
"""Build a deterministic, marker-checked consumer-GPU evidence archive.

Default mode validates and prints the selected archive plan. Pass --build only
after review; an existing output archive is never overwritten.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile
from typing import Any


ARCHIVE_ROOT = "triad-consumer-gpu-evidence"
STUDY_REL = Path("docs/validation-data/triad-v3/2026-11-01-consumer-gpu")
RAW_SUFFIXES = {".c128", ".u8"}
SOURCE_BUNDLES = (
    ("probe", "probe-source.tar.gz"),
    ("repair", "repair-source.tar.gz"),
    ("final-candidate", "final-candidate-source.tar.gz"),
    ("campaign-source-inputs", "campaign-source-inputs.tar.gz"),
)
EXPORTER_FILES = (
    "build/dev/triad_long",
    "tools/triad-long.cpp",
    "include/datapump/triad_modem_v3.hpp",
    "src/triad_simulation_stream.cpp",
)
TEST_FILES = (
    "tests/test_triad_exhaustive_correlation.py",
    "tests/test_triad_fp4_campaign.py",
    "tests/test_triad_scan_workspace.py",
    "tests/test_triad_tensor_native.py",
)
FIXED_EVIDENCE_FILES = (
    "generation.log",
    "final-candidate-hashes.json",
    "holdout-policy-freeze.json",
    "input-binding-check.json",
    "runpod-ui-observations.json",
    "runpod-ui-sustained.json",
    "partial-accumulation-probe.py",
    "scaling-diagnostic.json",
)
GENERIC_PRIVATE_PATTERNS = (
    re.compile(rb"-----BEGIN (?:OPENSSH |RSA |EC |DSA )?PRIVATE KEY-----", re.I),
    re.compile(rb"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b"),
    re.compile(rb"(?i)\bbearer\s+[A-Za-z0-9._~+/-]{20,}"),
    re.compile(rb"(?i)[\"']?(?:pod(?:_?id|_?name)|host(?:_?id|_?name|name)|hostname|instance_?id|api_?key|access_?key|secret_?key|private_?key|access_?token)[\"']?\s*[:=]\s*[\"']?[^\s,\"']{3,}"),
)
TEXT_SUFFIXES = {".py", ".json", ".txt", ".log", ".md", ".cpp", ".hpp",
                 ".h", ".yaml", ".yml", ".toml", ".ptx", ".sass", ".sh"}
EXCLUDED_NAME_PARTS = {"screenshot", "screenshots", "account", "accounts", "credentials"}
EXCLUDED_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp", ".pem", ".ppk", ".kdbx"}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def safe_member(member: str) -> str:
    path = PurePosixPath(member)
    if path.is_absolute() or not path.parts or any(part in ("", ".", "..") for part in path.parts):
        raise ValueError("unsafe archive member path encountered")
    return path.as_posix()


def skip_path(path: Path) -> bool:
    lower_parts = {part.lower() for part in path.parts}
    return ("__pycache__" in lower_parts or lower_parts.intersection(EXCLUDED_NAME_PARTS)
            or path.suffix.lower() in EXCLUDED_SUFFIXES or path.name.startswith("."))


def parent_head(source: Path) -> str:
    try:
        result = subprocess.run(["git", "rev-parse", "HEAD"], cwd=source,
                                capture_output=True, text=True, check=True, timeout=10)
        return result.stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return "unavailable"


def private_markers(path: Path | None) -> list[bytes]:
    if path is None:
        return []
    if not path.is_file():
        raise FileNotFoundError("private marker file is unavailable")
    markers = []
    for line in path.read_bytes().splitlines():
        marker = line.strip()
        if marker and not marker.startswith(b"#"):
            if len(marker) < 4:
                raise ValueError("private marker entries must be at least four bytes")
            markers.append(marker)
    return markers


def check_privacy(name: str, payload: bytes, markers: list[bytes]) -> None:
    if any(marker in payload for marker in markers):
        raise ValueError(f"private marker matched in selected member: {name}")
    lower = name.lower().encode("utf-8", errors="ignore")
    if any(marker in lower for marker in (b"runpod-pod-", b"provider-host-", b"account-token")):
        raise ValueError(f"private provider marker in selected member name: {name}")
    scan = payload
    suffix = Path(name).suffix.lower()
    if name.lower().endswith((".json.gz", ".trace.gz")):
        try:
            scan = gzip.decompress(payload)
        except (OSError, EOFError):
            scan = payload
    elif suffix not in TEXT_SUFFIXES:
        return
    if any(marker in scan for marker in markers):
        raise ValueError(f"private marker matched in decoded selected member: {name}")
    for pattern in GENERIC_PRIVATE_PATTERNS:
        if pattern.search(scan):
            raise ValueError(f"credential or private host/pod/key pattern matched in selected member: {name}")


def current_status(path: Path, evidence: Path) -> str:
    try:
        return path.resolve().relative_to(evidence.resolve()).as_posix()
    except ValueError:
        return path.name


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence-root", type=Path,
                        default=Path("local-evidence/triad-consumer-20261101"))
    parser.add_argument("--source-root", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path,
                        default=Path("local-evidence/triad-consumer-20261101/consumer-gpu-evidence.tar.gz"))
    parser.add_argument("--private-marker-file", type=Path, default=None,
                        help="optional external file with one private marker per line; never archived or echoed")
    parser.add_argument("--build", action="store_true",
                        help="write the deterministic archive; default validates and prints the plan only")
    args = parser.parse_args()
    evidence, source, output = (args.evidence_root.resolve(), args.source_root.resolve(), args.output.resolve())
    if not evidence.is_dir() or not source.is_dir():
        parser.error("evidence-root and source-root must exist")
    if args.build and output.exists():
        parser.error(f"refusing to overwrite existing output: {output}")
    marker_path = args.private_marker_file.resolve() if args.private_marker_file else None
    if marker_path is not None:
        try:
            marker_path.relative_to(source)
        except ValueError:
            pass
        else:
            parser.error("private marker file must be outside source-root and will not be archived")
    markers = private_markers(marker_path)
    members: dict[str, dict[str, Any]] = {}

    def add_bytes(member: str, logical_name: str, description: str, payload: bytes,
                  *, origin: str) -> None:
        member = safe_member(PurePosixPath(ARCHIVE_ROOT, member).as_posix())
        check_privacy(member, payload, markers)
        if member in members:
            raise ValueError(f"archive member collision: {member}")
        members[member] = {"member": member, "logical_name": logical_name,
                           "description": description, "origin": origin,
                           "payload": payload, "sha256": sha256_bytes(payload),
                           "size_bytes": len(payload), "mode": 0o644}

    def add_file(path: Path, member: str, logical_name: str, description: str) -> None:
        if not path.exists():
            raise FileNotFoundError(path)
        if path.is_symlink() or not path.is_file() or skip_path(path):
            return
        payload = path.read_bytes()
        origin = current_status(path, evidence) if path.is_relative_to(evidence) else path.relative_to(source).as_posix()
        mode = 0o755 if path.stat().st_mode & 0o111 else 0o644
        add_bytes(member, logical_name, description, payload, origin=origin)
        members[safe_member(PurePosixPath(ARCHIVE_ROOT, member).as_posix())]["mode"] = mode

    def add_tree(base: Path, archive_base: str, logical_base: str, description: str,
                 allow=lambda p: True) -> int:
        if not base.is_dir():
            raise FileNotFoundError(base)
        count = 0
        for path in sorted(base.rglob("*")):
            if path.is_file() and not path.is_symlink() and not skip_path(path) and allow(path):
                rel = path.relative_to(base).as_posix()
                add_file(path, PurePosixPath(archive_base, rel).as_posix(),
                         f"{logical_base}/{rel}", description)
                count += 1
        return count

    # Include all consumer stage-five output, including failed attempts, NPZ
    # score files, compiled instruction artifacts and compressed profile traces.
    stage5 = evidence / "stage5" / "consumer-results"
    stage5_count = add_tree(stage5, "stage5/consumer-results", "stage5", 
                            "Stage-five consumer GPU results, failures, profiles and instruction artifacts.")
    if stage5_count == 0:
        raise ValueError("stage5/consumer-results contained no files")

    add_tree(evidence / "cpu-representation", "cpu-representation", "cpu-representation",
             "Independent CPU sampled-waveform representation comparison.")

    # Holdout input provenance only: keep all manifests, index and per-case JSON
    # run records, but never package the 128 raw IQ/basis payloads.
    holdout = evidence / "holdout-inputs"
    holdout_count = add_tree(holdout, "holdout-inputs", "holdout-inputs",
        "Fresh holdout input manifests and provenance JSON; raw holdout IQ is intentionally excluded.",
        allow=lambda path: path.suffix.lower() == ".json")
    if holdout_count == 0:
        raise ValueError("holdout-inputs contained no JSON manifests/provenance")

    fixed_descriptions = {
        "generation.log": "Fresh input generation console log.",
        "final-candidate-hashes.json": "Frozen final candidate source and input hash record.",
        "holdout-policy-freeze.json": "Predeclared policy selection and holdout freeze.",
        "input-binding-check.json": "Holdout input-to-source provenance binding check.",
        "runpod-ui-observations.json": "Sanitized cloud UI observations for the benchmark instance.",
        "runpod-ui-sustained.json": "Sanitized cloud UI observations for sustained runs.",
        "partial-accumulation-probe.py": "Partial accumulation diagnostic source.",
        "scaling-diagnostic.json": "Input scaling and dynamic-range diagnostic.",
    }
    for rel, description in fixed_descriptions.items():
        add_file(evidence / rel, rel, rel, description)
    for path in sorted(evidence.glob("local-test*.log")) + sorted(evidence.glob("local-env-test*.log")):
        add_file(path, f"local-test-logs/{path.name}", path.name,
                 "Local regression command output for the isolated consumer study.")
    add_tree(evidence / "gpu-initial-tests", "gpu-initial-tests", "gpu-initial-tests",
             "Initial remote GPU functional test logs.")

    # Retain the original development capture with its producer record. This is
    # the one holdout-adjacent raw sampled input explicitly approved for archive.
    old_inputs = source / "local-evidence" / "triad-fp4-20261101" / "inputs"
    old_capture = old_inputs / "boundary-cn-10-r0"
    add_tree(old_capture, "development-inputs/boundary-cn-10-r0", "development-inputs",
             "Complete unchanged original development sampled capture, including raw waveform payloads.")
    add_file(old_inputs / "boundary-cn-10-r0.run.json",
             "development-inputs/boundary-cn-10-r0.run.json",
             "development-inputs/boundary-cn-10-r0.run.json",
             "Adjacent exporter invocation, environment and source identity for the original capture.")

    # Exact sampled-export binary and its three producer sources.
    for relative in EXPORTER_FILES:
        add_file(source / relative, f"exporter/{relative}", relative,
                 "Source-bound sampled waveform exporter or one of its exact producer sources.")

    # Current study scripts and tests are archived as final source snapshots.
    tool_files = sorted((source / "tools").glob("triad*.py"))
    if not tool_files:
        raise ValueError("no tools/triad*.py sources found")
    for path in tool_files:
        add_file(path, f"source-final/tools/{path.name}", f"tools/{path.name}",
                 "Final Triad source at package time.")
    for relative in TEST_FILES:
        add_file(source / relative, f"source-final/{relative}", relative,
                 "Relevant focused source regression test.")

    # Preserve each historical source variant without retaining its original
    # tar bundle. Only regular .py members are read and copied under unique roots.
    variants_added = {}
    for variant, bundle_name in SOURCE_BUNDLES:
        archive_path = evidence / bundle_name
        if not archive_path.is_file():
            raise FileNotFoundError(archive_path)
        count = 0
        try:
            with tarfile.open(archive_path, mode="r:gz") as bundle:
                for item in sorted(bundle.getmembers(), key=lambda row: row.name):
                    if not item.isfile() or PurePosixPath(item.name).suffix.lower() != ".py":
                        continue
                    original_member = safe_member(item.name)
                    source_stream = bundle.extractfile(item)
                    if source_stream is None:
                        raise ValueError(f"cannot read Python source member from {bundle_name}")
                    payload = source_stream.read()
                    member = PurePosixPath("source-variants", variant, original_member).as_posix()
                    add_bytes(member, f"{variant}:{original_member}",
                              f"Historical {variant} Python source snapshot extracted from the named bundle.",
                              payload, origin=f"{bundle_name}:{original_member}")
                    count += 1
        except (tarfile.TarError, OSError) as error:
            raise ValueError(f"cannot read source snapshot bundle {bundle_name}: {type(error).__name__}") from error
        if count == 0:
            raise ValueError(f"source snapshot bundle {bundle_name} contained no .py members")
        variants_added[variant] = count

    # Study protocol and reproducibility tools. README and plots are separately
    # maintained and deliberately omitted from this evidence archive.
    study_files = ("PROTOCOL.md", "generate-inputs.py", "run-campaign.py", "analyze.py",
                   "package-evidence.py")
    for name in study_files:
        path = source / STUDY_REL / name
        add_file(path, f"study/{name}", name,
                 "Maintained consumer-GPU experiment protocol or reproducibility tool.")

    if not members:
        raise ValueError("archive selection is empty")
    ordered = sorted(members.items())
    head = parent_head(source)
    manifest = {
        "schema": "triad-consumer-evidence-manifest-v1",
        "parent_head": head,
        "archive_root": ARCHIVE_ROOT,
        "selection": {"stage5_consumer_results_files": stage5_count,
                      "holdout_json_files": holdout_count,
                      "historical_source_variants": variants_added,
                      "raw_holdout_iq_included": False,
                      "original_development_raw_capture_included": True,
                      "readme_and_plots_included": False,
                      "original_source_bundles_included": False},
        "members": [{key: row[key] for key in
                     ("member", "logical_name", "description", "origin", "size_bytes", "sha256")}
                    for _, row in ordered],
        "privacy_scan": {"generic_credential_ssh_and_provider_field_patterns_checked": True,
                         "external_private_marker_file_used": marker_path is not None,
                         "marker_file_archived_or_echoed": False},
        "checksum_note": "SHA256SUMS covers all payload members and MANIFEST.json; it does not hash itself.",
    }
    manifest_bytes = (json.dumps(manifest, sort_keys=True, indent=2) + "\n").encode()
    manifest_member = safe_member(PurePosixPath(ARCHIVE_ROOT, "MANIFEST.json").as_posix())
    if manifest_member in members:
        raise ValueError("generated MANIFEST.json collides with selected payload")
    manifest_row = {"member": manifest_member, "logical_name": "generated:manifest",
                    "description": "Per-member provenance, artifact descriptions and source/input selection metadata.",
                    "origin": "generated from selected payloads and source revision",
                    "payload": manifest_bytes, "sha256": sha256_bytes(manifest_bytes),
                    "size_bytes": len(manifest_bytes), "mode": 0o644}
    output_members = ordered + [(manifest_member, manifest_row)]
    checksums = "".join(f"{row['sha256']}  {name}\n" for name, row in output_members).encode()
    checksum_member = safe_member(PurePosixPath(ARCHIVE_ROOT, "SHA256SUMS").as_posix())
    checksum_row = {"member": checksum_member, "logical_name": "generated:checksums",
                    "description": "SHA256 checksums for every payload and MANIFEST.json member.",
                    "origin": "generated", "payload": checksums,
                    "sha256": sha256_bytes(checksums), "size_bytes": len(checksums), "mode": 0o644}
    output_members += [(checksum_member, checksum_row)]
    uncompressed = sum(row["size_bytes"] for _, row in output_members)
    summary = {"mode": "build" if args.build else "dry-run",
               "output": str(output), "parent_head": head,
               "member_count": len(output_members), "uncompressed_bytes": uncompressed,
               "stage5_files": stage5_count, "holdout_json_files": holdout_count,
               "source_variants": variants_added,
               "private_marker_file_used": marker_path is not None,
               "members": [{"member": name, "size_bytes": row["size_bytes"],
                            "sha256": row["sha256"], "logical_name": row["logical_name"]}
                           for name, row in output_members]}
    print(json.dumps(summary, indent=2))
    if not args.build:
        return 0

    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        raise FileExistsError(f"refusing to overwrite existing output: {output}")
    try:
        with output.open("xb") as raw:
            with gzip.GzipFile(fileobj=raw, mode="wb", filename="", mtime=0, compresslevel=9) as compressed:
                with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
                    for name, row in output_members:
                        info = tarfile.TarInfo(name)
                        info.size = row["size_bytes"]
                        info.mtime = 0
                        info.uid = info.gid = 0
                        info.uname = info.gname = ""
                        info.mode = row["mode"]
                        archive.addfile(info, io.BytesIO(row["payload"]))
    except Exception:
        try:
            output.unlink()
        except OSError:
            pass
        raise
    print(f"wrote deterministic archive ({output.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

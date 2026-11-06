#!/usr/bin/env python3
"""Package compact weak-stream evidence without raw I/Q/audio arrays.

This utility only inventories, hashes, copies and verifies retained evidence;
it never runs simulations or receiver code.  The default action is a dry run.
Use --release only after the campaign owner confirms all data has settled.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import tarfile
import tempfile
from typing import BinaryIO


ROOT = Path(__file__).resolve().parents[4]
OUT_DIR = Path(__file__).resolve().parent
EVIDENCE_ROOT = ROOT / "local-evidence" / "triad-weak-stream"
PROTOCOL = EVIDENCE_ROOT / "independent" / "protocol.json"
LONGER_PROTOCOL = EVIDENCE_ROOT / "independent-longer" / "protocol.json"
ARCHIVE_NAME = "compact-evidence.tar.gz"
OUTER_MANIFEST_NAME = "artifact-manifest.json"
CHECKSUMS_NAME = "SHA256SUMS"
INTERNAL_MANIFEST_PATH = "weak-stream-evidence/package-inventory.json"
INTERNAL_PREFIX = "weak-stream-evidence/"

RETAINED_SUFFIXES = {".json", ".log", ".md", ".py"}
RAW_SUFFIXES = {".c128", ".u8", ".s16le", ".f32", ".pcm", ".npy", ".npz", ".wav"}
SKIP_PARTS = {"__pycache__"}

# These are the native qualification receiver's exact Python dependencies,
# declared by tools/triad_weak_qualify.py and frozen in independent/protocol.json.
EXTRA_SOURCE_PATHS = (
    "tools/triad_stream_frontend.py",
    "tools/triad_stream_run.py",
    "tools/triad_stream_scheduler.py",
    "tools/triad_stream_slots.py",
    "tests/test_triad_operational_admission.py",
    "tests/test_triad_segmented_fft.py",
    "tests/test_triad_stream_frontend.py",
    "tests/test_triad_stream_run.py",
    "tests/test_triad_stream_scheduler.py",
    "tests/test_triad_stream_slots.py",
    "tests/test_triad_weak_helper.py",
    "tests/test_triad_weak_receiver.py",
    "docs/README.md",
    "docs/triad-realtime.md",
    "docs/triad-performance-matrix.md",
    "docs/triad-weak-streaming.md",
    "tools/triad-helper-export.cpp",
)


class PackageError(RuntimeError):
    pass


def sha256_stream(stream: BinaryIO) -> tuple[str, int]:
    digest = hashlib.sha256()
    size = 0
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(block)
        size += len(block)
    return digest.hexdigest(), size


def sha256_file(path: Path) -> tuple[str, int]:
    with path.open("rb") as stream:
        return sha256_stream(stream)


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False,
                        allow_nan=False) + "\n").encode("utf-8")


def load_protocol() -> dict:
    if not PROTOCOL.is_file():
        raise PackageError(f"missing frozen independent protocol: {PROTOCOL}")
    try:
        value = json.loads(PROTOCOL.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise PackageError(f"cannot read independent protocol: {exc}") from exc
    if value.get("schema") != "triad-weak-independent-protocol-v1":
        raise PackageError("independent protocol schema is not the expected frozen weak-stream schema")
    if not isinstance(value.get("source_hashes"), dict):
        raise PackageError("protocol has no source_hashes mapping")
    return value


def load_longer_protocol() -> dict:
    if not LONGER_PROTOCOL.is_file():
        raise PackageError(f"missing frozen longer-run protocol: {LONGER_PROTOCOL}")
    try:
        value = json.loads(LONGER_PROTOCOL.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise PackageError(f"cannot read longer-run protocol: {exc}") from exc
    if value.get("schema") != "triad-weak-longer-protocol-v1":
        raise PackageError("longer protocol schema is not the expected frozen weak-stream schema")
    if not isinstance(value.get("source_hashes"), dict):
        raise PackageError("longer protocol has no source_hashes mapping")
    if not isinstance(value.get("additional_source_hashes"), dict):
        raise PackageError("longer protocol has no additional_source_hashes mapping")
    return value


def safe_relative(path: str) -> str:
    posix = PurePosixPath(path)
    if posix.is_absolute() or not posix.parts or any(part in ("", ".", "..") for part in posix.parts):
        raise PackageError(f"unsafe archive path: {path!r}")
    return posix.as_posix()


def evidence_candidates() -> tuple[list[Path], list[Path]]:
    retained: list[Path] = []
    skipped: list[Path] = []
    for path in sorted(EVIDENCE_ROOT.rglob("*")):
        if path.is_symlink():
            raise PackageError(f"symlink is not a packageable evidence path: {path}")
        if not path.is_file():
            continue
        rel = path.relative_to(EVIDENCE_ROOT)
        if any(part in SKIP_PARTS for part in rel.parts):
            skipped.append(path)
        elif path.suffix.lower() in RETAINED_SUFFIXES:
            retained.append(path)
        else:
            skipped.append(path)
    return retained, skipped


def source_candidates(protocol: dict) -> tuple[dict[str, str], dict[str, str]]:
    expected = protocol["source_hashes"]
    paths = set(expected)
    paths.update(EXTRA_SOURCE_PATHS)
    current: dict[str, str] = {}
    missing: list[str] = []
    mismatches: dict[str, dict[str, str]] = {}
    for rel in sorted(paths):
        path = ROOT / rel
        if path.is_symlink() or not path.is_file():
            missing.append(rel)
            continue
        digest, _ = sha256_file(path)
        current[rel] = digest
        if rel in expected and digest != expected[rel]:
            mismatches[rel] = {"protocol_sha256": expected[rel], "current_sha256": digest}
    if missing:
        raise PackageError("missing source files: " + ", ".join(missing))
    if mismatches:
        details = "; ".join(f"{k}: protocol={v['protocol_sha256']} current={v['current_sha256']}"
                             for k, v in mismatches.items())
        raise PackageError("frozen receiver sources do not match current source tree: " + details)
    return current, expected


def revision() -> str:
    try:
        result = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                check=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        raise PackageError(f"cannot record git HEAD: {exc}") from exc
    return result.stdout.strip()


def collect_files(protocol: dict) -> tuple[list[dict], list[dict], dict]:
    retained, skipped = evidence_candidates()
    sources, expected_sources = source_candidates(protocol)
    longer_protocol = load_longer_protocol()
    longer_sources, longer_expected_sources = source_candidates(longer_protocol)
    additional_sources: dict[str, str] = {}
    additional_source_archive_paths: dict[str, str] = {}
    evidence_by_path = {path.relative_to(ROOT).as_posix(): path for path in retained}
    for recorded_path, expected_digest in longer_protocol["additional_source_hashes"].items():
        path = Path(recorded_path)
        if path.is_absolute():
            try:
                relative = path.relative_to(ROOT)
            except ValueError as exc:
                raise PackageError(f"longer protocol source is outside repository: {recorded_path}") from exc
        else:
            relative = path
        relative_text = relative.as_posix()
        if relative_text not in evidence_by_path:
            raise PackageError(f"longer protocol's additional source is not selected as evidence: {relative_text}")
        actual_digest, _ = sha256_file(ROOT / relative)
        if actual_digest != expected_digest:
            raise PackageError(f"longer protocol additional source hash mismatch: {relative_text}")
        additional_sources[relative_text] = actual_digest
        evidence_relative = (ROOT / relative).relative_to(EVIDENCE_ROOT)
        additional_source_archive_paths[relative_text] = (
            f"{INTERNAL_PREFIX}local-evidence/triad-weak-stream/{evidence_relative.as_posix()}"
        )
    entries: list[dict] = []
    omitted: list[dict] = []

    def add_file(path: Path, archive_path: str, category: str) -> None:
        archive_path = safe_relative(archive_path)
        digest, size = sha256_file(path)
        entries.append({"archive_path": archive_path, "source_path": str(path.relative_to(ROOT)),
                        "size_bytes": size, "sha256": digest, "category": category})

    for path in retained:
        rel = path.relative_to(EVIDENCE_ROOT).as_posix()
        add_file(path, f"{INTERNAL_PREFIX}local-evidence/triad-weak-stream/{rel}", "study-output")

    # Preserve the exact source snapshot used for independent receiver rows,
    # then the new tools/tests used to diagnose and repair this study.
    source_paths = sorted(set(expected_sources) | set(longer_expected_sources) | set(EXTRA_SOURCE_PATHS))
    for rel in source_paths:
        add_file(ROOT / rel, f"{INTERNAL_PREFIX}source-snapshot/{rel}",
                 "frozen-receiver-source" if rel in expected_sources else "study-source-or-test")

    script_path = Path(__file__).resolve()
    add_file(script_path, f"{INTERNAL_PREFIX}package_evidence.py", "packaging-tool")

    # Hash omitted raw workspaces and PCM using bounded memory. These records
    # are metadata only; the binary files are never added to the archive.
    excluded_bytes = 0
    for path in skipped:
        rel = path.relative_to(EVIDENCE_ROOT)
        ext = path.suffix.lower()
        category = "raw-array-or-audio" if ext in RAW_SUFFIXES else "cache-or-unselected-file"
        digest, size = sha256_file(path)
        excluded_bytes += size
        omitted.append({"path": f"local-evidence/triad-weak-stream/{rel.as_posix()}",
                        "size_bytes": size, "sha256": digest, "suffix": ext,
                        "reason": "retained only as hash/size inventory; raw arrays/audio are not packaged"
                        if ext in RAW_SUFFIXES else "excluded cache or unsupported file type"})

    entries.sort(key=lambda item: item["archive_path"])
    omitted.sort(key=lambda item: item["path"])
    paths = [item["archive_path"] for item in entries]
    if len(paths) != len(set(paths)):
        raise PackageError("duplicate archive paths")
    record = {
        "schema": "triad-weak-stream-source-bound-evidence-v1",
        "git_head": revision(),
        "protocol_id": protocol["protocol_id"],
        "independent_protocol_sha256": sha256_file(PROTOCOL)[0],
        "longer_protocol_id": longer_protocol["protocol_id"],
        "longer_protocol_sha256": sha256_file(LONGER_PROTOCOL)[0],
        "expected_receiver_source_hashes": expected_sources,
        "expected_longer_receiver_source_hashes": longer_expected_sources,
        "longer_current_source_hashes": longer_sources,
        "longer_additional_source_hashes": additional_sources,
        "longer_additional_source_archive_paths": additional_source_archive_paths,
        "current_source_hashes": sources,
        "native_exporter": native_exporter_identity(protocol),
        "environment": protocol.get("environment", {}),
        "entry_count": len(entries),
        "entries": entries,
        "omitted_raw_or_other_files": omitted,
        "omitted_file_count": len(omitted),
        "omitted_bytes": excluded_bytes,
        "interpretation": "Receiver source hashes are checked against the independent protocol. Native exports reuse the identified build/dev-cli binary; no fresh source-to-binary certification is asserted. Large raw workspaces and PCM/audio are represented by streamed SHA-256 and sizes only.",
    }
    return entries, omitted, record


def native_exporter_identity(protocol: dict) -> dict:
    exporter = protocol.get("exporter", {})
    path_text = exporter.get("path")
    binary_path = Path(path_text) if isinstance(path_text, str) else None
    if binary_path is not None and not binary_path.is_absolute():
        binary_path = ROOT / binary_path
    expected_binary = exporter.get("sha256")
    binary_now = None
    binary_size = None
    if binary_path is not None and binary_path.is_file():
        binary_now, binary_size = sha256_file(binary_path)
    if expected_binary and binary_now is not None and binary_now != expected_binary:
        raise PackageError("current dev-cli exporter binary differs from the binary identified by the independent protocol")
    cpp = ROOT / "tools" / "triad-helper-export.cpp"
    cmake_cache = ROOT / "build" / "dev-cli" / "CMakeCache.txt"
    ninja_file = ROOT / "build" / "dev-cli" / "build.ninja"
    return {
        "protocol_binary_path": path_text,
        "protocol_binary_sha256": expected_binary,
        "current_binary_sha256": binary_now,
        "current_binary_size_bytes": binary_size,
        "binary_matches_protocol": binary_now == expected_binary if expected_binary else None,
        "build_profile": "build/dev-cli",
        "cmake_cache_sha256": sha256_file(cmake_cache)[0] if cmake_cache.is_file() else None,
        "build_ninja_sha256": sha256_file(ninja_file)[0] if ninja_file.is_file() else None,
        "exporter_cpp_path": "tools/triad-helper-export.cpp",
        "exporter_cpp_sha256": sha256_file(cpp)[0] if cpp.is_file() else None,
        "provenance_scope": exporter.get("provenance_scope"),
    }


def write_tar(archive_path: Path, entries: list[dict], inventory: dict) -> list[dict]:
    """Create deterministic gzip-tar; only regular files are emitted."""
    internal = json_bytes(inventory)
    packaged = entries + [{"archive_path": INTERNAL_MANIFEST_PATH,
                           "source_path": None, "size_bytes": len(internal),
                           "sha256": hashlib.sha256(internal).hexdigest(),
                           "category": "internal-manifest"}]
    if len({e["archive_path"] for e in packaged}) != len(packaged):
        raise PackageError("internal manifest path collides with evidence")
    with archive_path.open("wb") as raw_file:
        with gzip.GzipFile(fileobj=raw_file, mode="wb", filename="", mtime=0, compresslevel=6) as gz:
            with tarfile.open(fileobj=gz, mode="w|", format=tarfile.PAX_FORMAT) as tar:
                for entry in sorted(packaged, key=lambda x: x["archive_path"]):
                    name = safe_relative(entry["archive_path"])
                    info = tarfile.TarInfo(name)
                    info.type = tarfile.REGTYPE
                    info.mode = 0o644
                    info.uid = 0
                    info.gid = 0
                    info.uname = ""
                    info.gname = ""
                    info.mtime = 0
                    info.size = entry["size_bytes"]
                    if entry["category"] == "internal-manifest":
                        source = io.BytesIO(internal)
                    else:
                        source = (ROOT / entry["source_path"]).open("rb")
                    try:
                        tar.addfile(info, source)
                    finally:
                        source.close()
    return packaged


def release() -> dict:
    protocol = load_protocol()
    entries, omitted, inventory = collect_files(protocol)
    archive = OUT_DIR / ARCHIVE_NAME
    manifest_path = OUT_DIR / OUTER_MANIFEST_NAME
    checksums = OUT_DIR / CHECKSUMS_NAME
    if any(path.exists() for path in (archive, manifest_path, checksums)):
        raise PackageError("release outputs already exist; refusing to overwrite frozen evidence")
    tmp = OUT_DIR / (ARCHIVE_NAME + ".tmp")
    try:
        packaged = write_tar(tmp, entries, inventory)
        archive_hash, archive_size = sha256_file(tmp)
        manifest = {
            "schema": "triad-weak-stream-artifact-manifest-v1",
            "archive": ARCHIVE_NAME,
            "archive_size_bytes": archive_size,
            "archive_sha256": archive_hash,
            "member_count": len(packaged),
            "study_output_count": sum(x["category"] == "study-output" for x in entries),
            "source_snapshot_count": sum(x["category"] in ("frozen-receiver-source", "study-source-or-test") for x in entries),
            "omitted_raw_input_count": len([x for x in omitted if x["reason"].startswith("retained only")]),
            "omitted_raw_input_bytes": inventory["omitted_bytes"],
            "git_head": inventory["git_head"],
            "protocol_id": inventory["protocol_id"],
            "independent_protocol_sha256": inventory["independent_protocol_sha256"],
            "internal_inventory_sha256": hashlib.sha256(json_bytes(inventory)).hexdigest(),
            "source_hashes": inventory["current_source_hashes"],
            "native_exporter": inventory["native_exporter"],
            "environment": inventory["environment"],
            "archive_members": [{"path": x["archive_path"], "size_bytes": x["size_bytes"], "sha256": x["sha256"], "category": x["category"]} for x in packaged],
            "limitation": "Archive preserves machine-readable outputs and source/provenance metadata, but not raw PCM, I/Q captures, templates, or validity arrays. Those inputs are inventoried by path, size and SHA-256 for local reproduction.",
        }
        tmp.replace(archive)
        manifest_path.write_bytes(json_bytes(manifest))
        sums = []
        for path in (archive, manifest_path):
            digest, _ = sha256_file(path)
            sums.append(f"{digest}  {path.name}")
        checksums.write_text("\n".join(sums) + "\n")
        return manifest
    except Exception:
        tmp.unlink(missing_ok=True)
        raise


def verify() -> dict:
    archive = OUT_DIR / ARCHIVE_NAME
    manifest_path = OUT_DIR / OUTER_MANIFEST_NAME
    if not archive.is_file() or not manifest_path.is_file():
        raise PackageError("archive or artifact manifest is missing")
    manifest = json.loads(manifest_path.read_text())
    digest, size = sha256_file(archive)
    if digest != manifest.get("archive_sha256") or size != manifest.get("archive_size_bytes"):
        raise PackageError("archive hash or size does not match artifact manifest")
    expected = {row["path"]: row for row in manifest.get("archive_members", [])}
    seen: set[str] = set()
    internal_inventory = None
    with tarfile.open(archive, "r:gz") as tar:
        for member in tar:
            name = safe_relative(member.name)
            if name in seen:
                raise PackageError(f"duplicate archive member: {name}")
            seen.add(name)
            if not member.isfile() or member.issym() or member.islnk():
                raise PackageError(f"non-regular archive member: {name}")
            row = expected.get(name)
            if row is None or member.size != row["size_bytes"]:
                raise PackageError(f"unexpected archive member or size: {name}")
            stream = tar.extractfile(member)
            if stream is None:
                raise PackageError(f"cannot read archive member: {name}")
            member_hash, member_size = sha256_stream(stream)
            if member_hash != row["sha256"] or member_size != row["size_bytes"]:
                raise PackageError(f"archive member checksum mismatch: {name}")
            if name == INTERNAL_MANIFEST_PATH:
                stream = tar.extractfile(member)
                internal_inventory = json.load(stream)
    if seen != set(expected):
        raise PackageError("archive member set differs from manifest")
    if internal_inventory is None:
        raise PackageError("source-bound inventory is absent from archive")
    expected_sources = internal_inventory["expected_receiver_source_hashes"]
    source_entry_hashes = {
        row["archive_path"].removeprefix(INTERNAL_PREFIX + "source-snapshot/"): row["sha256"]
        for row in internal_inventory["entries"]
        if row["category"] == "frozen-receiver-source"
    }
    if source_entry_hashes != expected_sources:
        raise PackageError("frozen receiver source snapshot does not match protocol source hashes")
    longer_expected_sources = internal_inventory["expected_longer_receiver_source_hashes"]
    longer_source_entry_hashes = {
        row["archive_path"].removeprefix(INTERNAL_PREFIX + "source-snapshot/"): row["sha256"]
        for row in internal_inventory["entries"]
        if row["category"] == "frozen-receiver-source"
    }
    if any(longer_source_entry_hashes.get(path) != digest
           for path, digest in longer_expected_sources.items()):
        raise PackageError("frozen longer-run receiver source snapshot does not match protocol source hashes")
    additional_hashes = internal_inventory["longer_additional_source_hashes"]
    additional_paths = internal_inventory["longer_additional_source_archive_paths"]
    archive_entry_hashes = {row["archive_path"]: row["sha256"] for row in internal_inventory["entries"]}
    if any(archive_entry_hashes.get(additional_paths[path]) != digest
           for path, digest in additional_hashes.items()):
        raise PackageError("longer protocol additional source is not preserved byte-for-byte in archive")
    if internal_inventory.get("protocol_id") != manifest.get("protocol_id"):
        raise PackageError("internal source-bound protocol identity mismatch")
    sums_path = OUT_DIR / CHECKSUMS_NAME
    if sums_path.is_file():
        checks = {}
        for line in sums_path.read_text().splitlines():
            digest_text, name = line.split("  ", 1)
            checks[name] = digest_text
        for path in (archive, manifest_path):
            if checks.get(path.name) != sha256_file(path)[0]:
                raise PackageError(f"SHA256SUMS mismatch: {path.name}")
    return {"archive_sha256": digest, "archive_size_bytes": size,
            "verified_members": len(seen), "receiver_sources": len(source_entry_hashes),
            "protocol_id": internal_inventory["protocol_id"]}


def dry_run() -> dict:
    protocol = load_protocol()
    retained, skipped = evidence_candidates()
    sources, expected = source_candidates(protocol)
    retained_bytes = sum(path.stat().st_size for path in retained)
    skipped_bytes = sum(path.stat().st_size for path in skipped)
    output = {
        "mode": "dry-run; no archive or evidence inventory frozen",
        "evidence_root": str(EVIDENCE_ROOT),
        "protocol_id": protocol["protocol_id"],
        "retained_outputs": len(retained),
        "retained_uncompressed_bytes": retained_bytes,
        "excluded_files": len(skipped),
        "excluded_bytes": skipped_bytes,
        "excluded_raw_array_audio_files": sum(path.suffix.lower() in RAW_SUFFIXES for path in skipped),
        "receiver_source_count": len(expected),
        "new_source_or_test_count": len(set(EXTRA_SOURCE_PATHS)-set(expected)),
        "source_hashes_match_protocol": all(sources.get(path) == digest for path, digest in expected.items()),
        "archive_not_created": True,
    }
    return output


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--release", action="store_true", help="freeze archive and manifests after owner release")
    action.add_argument("--verify", action="store_true", help="verify existing archive, hashes and source-bound records")
    args = parser.parse_args(argv)
    try:
        if args.release:
            result = release()
        elif args.verify:
            result = verify()
        else:
            result = dry_run()
    except (PackageError, OSError, ValueError, json.JSONDecodeError, tarfile.TarError) as exc:
        parser.exit(2, f"package_evidence.py: error: {exc}\n")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

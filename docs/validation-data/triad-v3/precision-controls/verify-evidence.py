#!/usr/bin/env python3
"""Verify an evidence catalog's archives, selected members and repo copies.

This validator only reads files. It never extracts archive members, executes
payloads, accesses the network, or claims receiver qualification.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tarfile
from typing import Any


SCHEMA = "triad-precision-evidence-catalog-v1"
ROOT = Path(__file__).resolve().parents[4]


def digest_file(path: Path) -> tuple[str, int]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
            size += len(block)
    return digest.hexdigest(), size


def digest_stream(stream: Any) -> tuple[str, int]:
    digest = hashlib.sha256()
    size = 0
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(block)
        size += len(block)
    return digest.hexdigest(), size


def safe_relative_repo(value: Any, field: str) -> tuple[str, Path]:
    if not isinstance(value, str) or not value:
        raise ValueError(f"{field} must be a nonempty repository-relative path")
    path = PurePosixPath(value)
    if (path.is_absolute() or "\\" in value or value != path.as_posix() or
            any(part in ("", ".", "..") for part in path.parts)):
        raise ValueError(f"{field} is not a safe normalized repository-relative path")
    result = ROOT.joinpath(*path.parts)
    resolved_root = ROOT.resolve()
    resolved = result.resolve(strict=False)
    try:
        resolved.relative_to(resolved_root)
    except ValueError as error:
        raise ValueError(f"{field} resolves outside the repository") from error
    return path.as_posix(), result


def safe_archive_member(value: Any) -> str:
    if not isinstance(value, str) or not value:
        raise ValueError("archive member path must be a nonempty string")
    path = PurePosixPath(value)
    if (path.is_absolute() or "\\" in value or value != path.as_posix() or
            any(part in ("", ".", "..") for part in path.parts)):
        raise ValueError("archive member path is not safe and normalized")
    return path.as_posix()


def expected_digest(value: Any, field: str) -> str:
    if (not isinstance(value, str) or len(value) != 64 or
            any(character not in "0123456789abcdef" for character in value.lower())):
        raise ValueError(f"{field} must be a 64-character SHA-256 hex digest")
    return value.lower()


def expected_size(value: Any, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise ValueError(f"{field} must be a nonnegative integer")
    return value


def load_catalog(path: Path) -> dict[str, Any]:
    catalog = json.loads(path.read_text())
    if not isinstance(catalog, dict) or catalog.get("schema") != SCHEMA:
        raise ValueError(f"catalog schema must be {SCHEMA}")
    if not isinstance(catalog.get("archives"), list) or not isinstance(catalog.get("groups"), list):
        raise ValueError("catalog archives and groups must be arrays")
    if not isinstance(catalog.get("copies", []), list):
        raise ValueError("catalog copies must be an array")
    return catalog


def verify_archive_record(row: dict[str, Any], errors: list[str], relevant_paths: set[str]):
    archive_id = row.get("id")
    if not isinstance(archive_id, str) or not archive_id:
        errors.append("archive record has invalid id")
        return None
    try:
        relative, path = safe_relative_repo(row.get("path"), f"archive {archive_id} path")
        relevant_paths.add(relative)
        expected_hash = expected_digest(row.get("sha256"), f"archive {archive_id} sha256")
        expected_bytes = expected_size(row.get("bytes"), f"archive {archive_id} bytes")
        if path.is_symlink() or not path.is_file():
            errors.append(f"archive {archive_id} is missing or not a regular file")
            return {"id": archive_id, "path": relative, "status": "missing_or_unsafe",
                    "sha256": None, "bytes": None, "expected_sha256": expected_hash,
                    "expected_bytes": expected_bytes}
        actual_hash, actual_bytes = digest_file(path)
        status = "verified" if actual_hash == expected_hash and actual_bytes == expected_bytes else "mismatch"
        if status != "verified":
            errors.append(f"archive {archive_id} hash or byte count mismatch")
        return {"id": archive_id, "path": relative, "status": status,
                "sha256": actual_hash, "bytes": actual_bytes,
                "expected_sha256": expected_hash, "expected_bytes": expected_bytes,
                "absolute_path": path}
    except (TypeError, ValueError, OSError) as error:
        errors.append(f"archive {archive_id} invalid: {error}")
        return {"id": archive_id, "status": "invalid"}


def verify_tar_members(archive_path: Path, expected: dict[str, dict[str, Any]]) -> dict[str, dict[str, Any]]:
    """Stream the archive once, hashing selected regular members as encountered."""
    counts = {name: 0 for name in expected}
    results: dict[str, dict[str, Any]] = {}
    with tarfile.open(archive_path, mode="r|*") as archive:
        for item in archive:
            if item.name not in expected:
                continue
            name = item.name
            counts[name] += 1
            if counts[name] > 1:
                results[name] = {"status": "duplicate", "occurrences": counts[name]}
                continue
            if not item.isfile() or item.issym() or item.islnk():
                results[name] = {"status": "not_regular"}
                continue
            stream = archive.extractfile(item)
            if stream is None:
                results[name] = {"status": "read_failed"}
                continue
            actual_hash, actual_bytes = digest_stream(stream)
            catalog_row = expected[name]
            status = ("verified" if actual_hash == catalog_row["sha256"] and
                      actual_bytes == catalog_row["bytes"] else "mismatch")
            results[name] = {"status": status, "sha256": actual_hash, "bytes": actual_bytes}
    for name, count in counts.items():
        if count == 0:
            results[name] = {"status": "missing"}
    return results


def status_for_repo_files(paths: set[str], root: Path) -> dict[str, str]:
    if not paths:
        return {}
    command_paths = sorted(paths)
    try:
        status = subprocess.run(
            ["git", "status", "--porcelain=v1", "-z", "--untracked-files=all",
             "--ignored=matching", "--", *command_paths],
            cwd=root, capture_output=True, check=False, timeout=15)
        tracked = subprocess.run(["git", "ls-files", "-z", "--", *command_paths],
                                 cwd=root, capture_output=True, check=False, timeout=15)
    except (OSError, subprocess.SubprocessError):
        return {path: "unavailable" for path in command_paths}
    if status.returncode != 0 or tracked.returncode != 0:
        return {path: "unavailable" for path in command_paths}
    tracked_set = {part.decode("utf-8", errors="replace") for part in tracked.stdout.split(b"\0") if part}
    state_by_path: dict[str, str] = {}
    for chunk in status.stdout.split(b"\0"):
        if not chunk:
            continue
        if len(chunk) < 3:
            continue
        code = chunk[:2].decode("ascii", errors="replace")
        name = chunk[3:].decode("utf-8", errors="replace")
        if code == "!!":
            state_by_path[name] = "ignored"
        elif code == "??":
            state_by_path[name] = "untracked"
        else:
            state_by_path[name] = "tracked"
    return {path: ("ignored" if state_by_path.get(path) == "ignored" else
                   "untracked" if path not in tracked_set else "tracked")
            for path in command_paths}


def verify(catalog: dict[str, Any], catalog_path: Path) -> dict[str, Any]:
    errors: list[str] = []
    relevant_paths: set[str] = set()
    archives: dict[str, dict[str, Any]] = {}
    archive_rows: list[dict[str, Any]] = []
    for row in catalog["archives"]:
        if not isinstance(row, dict):
            errors.append("archive entry is not an object")
            continue
        result = verify_archive_record(row, errors, relevant_paths)
        if result is None:
            continue
        archive_id = result.get("id")
        if archive_id in archives:
            errors.append(f"duplicate archive id: {archive_id}")
        else:
            archives[archive_id] = result
        archive_rows.append(result)

    groups = catalog["groups"]
    group_ids: set[str] = set()
    selections: dict[tuple[str, str], dict[str, Any]] = {}
    group_summaries = []
    for group in groups:
        if not isinstance(group, dict):
            errors.append("group entry is not an object")
            continue
        group_id = group.get("id")
        archive_id = group.get("archive_id")
        if not isinstance(group_id, str) or not group_id or group_id in group_ids:
            errors.append(f"invalid or duplicate group id: {group_id}")
        else:
            group_ids.add(group_id)
        if archive_id not in archives:
            errors.append(f"group {group_id} references unknown archive {archive_id}")
        items = group.get("members")
        if not isinstance(items, list):
            errors.append(f"group {group_id} members must be an array")
            continue
        unique_in_group: set[tuple[str, str]] = set()
        for item in items:
            if not isinstance(item, dict):
                errors.append(f"group {group_id} has a non-object member")
                continue
            try:
                member = safe_archive_member(item.get("path"))
                digest = expected_digest(item.get("sha256"), f"group {group_id} member sha256")
                size = expected_size(item.get("bytes"), f"group {group_id} member bytes")
            except (TypeError, ValueError) as error:
                errors.append(f"group {group_id} member invalid: {error}")
                continue
            key = (str(archive_id), member)
            unique_in_group.add(key)
            value = {"archive_id": archive_id, "member": member,
                     "sha256": digest, "bytes": size}
            old = selections.get(key)
            if old and (old["sha256"] != digest or old["bytes"] != size):
                errors.append(f"conflicting catalog hashes for repeated member in {group_id}")
            else:
                selections.setdefault(key, value)
        group_summaries.append({"id": group_id, "title": group.get("title"),
                                "scope": group.get("scope"),
                                "member_references": len(items),
                                "unique_members": len(unique_in_group),
                                "archive_id": archive_id})

    archive_member_results: dict[tuple[str, str], dict[str, Any]] = {}
    by_archive: dict[str, set[str]] = {}
    for archive_id, member in selections:
        by_archive.setdefault(archive_id, set()).add(member)
    for archive_id, names in by_archive.items():
        archive_row = archives.get(archive_id)
        if not archive_row or archive_row.get("status") != "verified":
            for member in names:
                archive_member_results[(archive_id, member)] = {"status": "archive_unverified"}
            continue
        archive_path = archive_row["absolute_path"]
        try:
            expected = {member: selections[(archive_id, member)] for member in names}
            streamed = verify_tar_members(archive_path, expected)
        except (OSError, tarfile.TarError) as error:
            errors.append(f"archive {archive_id} cannot be read as tar: {type(error).__name__}")
            for member in names:
                archive_member_results[(archive_id, member)] = {"status": "archive_unreadable"}
            continue
        for member in names:
            result = streamed.get(member, {"status": "missing"})
            if result["status"] == "missing":
                errors.append(f"selected member missing from archive {archive_id}")
            elif result["status"] == "duplicate":
                errors.append(f"ambiguous duplicate tar member in archive {archive_id}")
            elif result["status"] == "not_regular":
                errors.append(f"selected member is not a regular file in archive {archive_id}")
            elif result["status"] == "read_failed":
                errors.append(f"selected member read failed in archive {archive_id}")
            elif result["status"] == "mismatch":
                errors.append(f"selected member digest or size mismatch in archive {archive_id}")
            archive_member_results[(archive_id, member)] = result

    copy_summaries = []
    copy_paths: set[str] = set()
    for copy in catalog.get("copies", []):
        if not isinstance(copy, dict):
            errors.append("copy entry is not an object")
            continue
        copy_id = copy.get("archive_id")
        try:
            relative, path = safe_relative_repo(copy.get("path"), "copy path")
            member = safe_archive_member(copy.get("member"))
            expected_hash = expected_digest(copy.get("sha256"), "copy sha256")
        except (TypeError, ValueError) as error:
            errors.append(f"copy entry invalid: {error}")
            continue
        relevant_paths.add(relative)
        copy_paths.add(relative)
        key = (str(copy_id), member)
        archive_member = archive_member_results.get(key)
        if archive_member is None:
            errors.append("copy refers to a member not selected by any group")
        if path.is_symlink() or not path.is_file():
            errors.append(f"repository copy is missing or not a regular file: {relative}")
            copy_summaries.append({"path": relative, "archive_id": copy_id,
                                   "member": member, "status": "missing_or_unsafe"})
            continue
        actual_hash, actual_bytes = digest_file(path)
        match = (archive_member is not None and archive_member.get("status") == "verified" and
                 actual_hash == expected_hash == archive_member.get("sha256") and
                 actual_bytes == archive_member.get("bytes"))
        if not match:
            errors.append(f"repository copy differs from selected archive member: {relative}")
        copy_summaries.append({"path": relative, "archive_id": copy_id, "member": member,
                               "status": "verified" if match else "mismatch",
                               "sha256": actual_hash, "bytes": actual_bytes})

    git_status = status_for_repo_files(relevant_paths, ROOT)
    ignored = sorted(path for path, status in git_status.items() if status == "ignored")
    if ignored:
        errors.append(f"{len(ignored)} catalog archive/copy files are git-ignored")
    report = {
        "schema": "triad-precision-evidence-validation-v1",
        "status": "passed" if not errors else "failed",
        "scope": "archive bytes, selected regular-file members, declared hashes/sizes, repository copies and repository status",
        "qualification_claim": "none; this validates evidence integrity only",
        "catalog_basename": catalog_path.name,
        "catalog_sha256": digest_file(catalog_path)[0],
        "validator_sha256": digest_file(Path(__file__))[0],
        "python_version": sys.version,
        "counts": {"archives": len(archive_rows), "groups": len(group_summaries),
                   "unique_selected_archive_members": len(selections),
                   "group_member_references": sum(row["member_references"] for row in group_summaries),
                   "repository_copies": len(copy_summaries),
                   "tracked_files": sum(value == "tracked" for value in git_status.values()),
                   "untracked_files": sum(value == "untracked" for value in git_status.values()),
                   "ignored_files": len(ignored),
                   "git_status_available": all(value != "unavailable" for value in git_status.values())},
        "archives": [{key: value for key, value in row.items() if key != "absolute_path"}
                     for row in archive_rows],
        "groups": group_summaries,
        "repository_copies": copy_summaries,
        "git_status": git_status,
        "errors": errors[:30],
        "additional_error_count": max(0, len(errors)-30),
    }
    return report


def main() -> int:
    global ROOT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT,
                        help="repository root (default is derived from this script path)")
    parser.add_argument("--catalog", type=Path, default=Path(__file__).with_name("catalog.json"),
                        help="precision evidence catalog JSON")
    parser.add_argument("--output", type=Path, default=None,
                        help="optional JSON report path; existing files are not overwritten")
    args = parser.parse_args()
    ROOT = args.root.resolve()
    catalog_path = args.catalog if args.catalog.is_absolute() else ROOT / args.catalog
    if not catalog_path.is_file():
        report = {"schema": "triad-precision-evidence-validation-v1", "status": "failed",
                  "scope": "catalog validation", "qualification_claim": "none",
                  "errors": ["catalog file not found"]}
        exit_code = 1
    else:
        try:
            report = verify(load_catalog(catalog_path), catalog_path)
            exit_code = 0 if report["status"] == "passed" else 1
        except (OSError, json.JSONDecodeError, TypeError, ValueError) as error:
            report = {"schema": "triad-precision-evidence-validation-v1", "status": "failed",
                      "scope": "catalog validation", "qualification_claim": "none",
                      "errors": [f"{type(error).__name__}: {error}"]}
            exit_code = 1
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output is None:
        print(encoded, end="")
    else:
        output = args.output.resolve()
        if output.exists():
            parser.error(f"refusing to overwrite report: {output}")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(encoded)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())

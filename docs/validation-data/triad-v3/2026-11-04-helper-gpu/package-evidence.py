#!/usr/bin/env python3
"""Build the compact, source-bound Helper/GPU evidence package.

Run with ``--dry-run`` while evidence is still changing. Packaging requires the
explicit ``--release`` flag and refuses to overwrite an existing record. Large
native sample/workspace arrays are streamed into a raw-input inventory and are
not embedded in the compact archive.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import subprocess
import tarfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
EVIDENCE = ROOT / "local-evidence/triad-helper-gpu"
ARCHIVE = HERE / "compact-evidence.tar.gz"
MANIFEST = HERE / "artifact-manifest.json"
INVENTORY = HERE / "raw-input-inventory.json"
SUMS = HERE / "SHA256SUMS"

TEXT_SUFFIXES = {
    ".json", ".py", ".log", ".txt", ".md", ".sh", ".cpp", ".hpp",
    ".h", ".cu", ".cuh", ".csv", ".jsonl", ".sha256", ".yaml", ".yml", ".toml",
}
RAW_SUFFIXES = {
    ".c128", ".u8", ".c64", ".f32", ".s16le", ".npy", ".npz",
    ".bin", ".raw", ".pcm", ".wav", ".iq", ".iq16", ".dat", ".pt",
}
FORBIDDEN_PATH_PARTS = {"tmp", "triad-helper-gpu-cloud", "__pycache__"}


def digest_stream(stream) -> tuple[str, int]:
    h = hashlib.sha256()
    size = 0
    while True:
        block = stream.read(1024 * 1024)
        if not block:
            break
        h.update(block)
        size += len(block)
    return h.hexdigest(), size


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return digest_stream(stream)[0]


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n")


def permitted(path: Path) -> bool:
    if path.is_symlink() or not path.is_file():
        return False
    try:
        relative = path.relative_to(EVIDENCE)
    except ValueError:
        return False
    return not any(part.lower() in FORBIDDEN_PATH_PARTS for part in relative.parts)


def raw_inventory() -> dict:
    """Hash every sanitized source input, including raw arrays, by streaming."""
    if not EVIDENCE.is_dir():
        raise FileNotFoundError(f"expected evidence directory: {EVIDENCE}")
    entries = []
    for path in sorted(EVIDENCE.rglob("*")):
        if not permitted(path) or path.suffix.lower() not in RAW_SUFFIXES | TEXT_SUFFIXES:
            continue
        with path.open("rb") as stream:
            checksum, size = digest_stream(stream)
        entries.append({
            "path": path.relative_to(ROOT).as_posix(),
            "bytes": size,
            "sha256": checksum,
            "kind": "native_sample_or_numerical_workspace" if path.suffix.lower() in RAW_SUFFIXES
                    else "textual_result_log_or_source",
            "embedded": path.suffix.lower() in TEXT_SUFFIXES,
        })
    return {
        "schema": "triad-helper-gpu-raw-input-inventory-v1",
        "scope": EVIDENCE.relative_to(ROOT).as_posix(),
        "entry_count": len(entries),
        "raw_array_count": sum(item["kind"] == "native_sample_or_numerical_workspace" for item in entries),
        "text_input_count": sum(item["kind"] == "textual_result_log_or_source" for item in entries),
        "total_bytes": sum(item["bytes"] for item in entries),
        "entries": entries,
        "note": (
            "Native sample captures, templates, validity masks and numerical arrays are streamed "
            "through SHA-256 and excluded from the compact archive. Reproduce them using the "
            "archived manifests, scripts and source snapshots. Anything under tmp or "
            "triad-helper-gpu-cloud is excluded before inventory or packaging."
        ),
    }


def source_identity_check() -> dict:
    """Verify every declared frozen code snapshot against its exact hash."""
    identity_path = EVIDENCE / "source-identity.json"
    if not permitted(identity_path):
        raise FileNotFoundError(f"missing source identity bundle: {identity_path}")
    identity = json.loads(identity_path.read_text())
    git_base = identity.get("git_base")
    if not isinstance(git_base, str) or len(git_base) != 40 or any(c not in "0123456789abcdef" for c in git_base.lower()):
        raise ValueError("source identity must declare a 40-hex git_base")
    roles = identity.get("runtime_snapshots")
    if not isinstance(roles, dict) or not roles:
        raise ValueError("source identity has no runtime_snapshots map")
    missing = []
    mismatched = []
    checked = 0
    for role, snapshots in roles.items():
        if not isinstance(snapshots, dict) or not snapshots:
            raise ValueError(f"empty/invalid runtime snapshot role: {role}")
        for source_name, expected in snapshots.items():
            if not isinstance(source_name, str) or Path(source_name).is_absolute() or ".." in Path(source_name).parts:
                raise ValueError(f"unsafe source snapshot name in role {role}: {source_name!r}")
            snapshot = EVIDENCE / "runtime-snapshots" / role / source_name
            if not permitted(snapshot):
                missing.append(f"{role}/{source_name}")
                continue
            if not isinstance(expected, str) or sha256(snapshot) != expected:
                mismatched.append(f"{role}/{source_name}")
                continue
            checked += 1
    return {
        "git_base": git_base,
        "roles": len(roles),
        "snapshots_checked": checked,
        "missing": missing,
        "mismatched": mismatched,
        "provenance_notes": identity.get("provenance_notes", {}),
        "ok": not missing and not mismatched,
    }


def archive_files() -> dict[str, Path]:
    """Select sanitized technical outputs, scripts and source snapshots only."""
    if not EVIDENCE.is_dir():
        raise FileNotFoundError(f"expected evidence directory: {EVIDENCE}")
    files: dict[str, Path] = {}
    for path in sorted(EVIDENCE.rglob("*")):
        if not permitted(path):
            continue
        if path.suffix.lower() not in TEXT_SUFFIXES:
            continue
        relative = path.relative_to(EVIDENCE)
        # Preserve the invalid early central estimate as historical evidence,
        # while making its superseded status explicit in its archive name.
        if relative.as_posix() in {"clock-loss/central-e6-b0.json", "clock-loss/central-e6-b0.log"}:
            relative = Path("clock-loss/superseded-initial") / (path.stem + ".superseded" + path.suffix)
        member = "evidence/" + relative.as_posix()
        files[member] = path

    # Include this exact packager and explicit identity bundle if the parent
    # has supplied one. Those files distinguish the historical baseline from
    # current source snapshots without embedding a workspace-wide git diff.
    files["evidence/package-evidence.py"] = Path(__file__).resolve()
    identity = EVIDENCE / "source-identity.json"
    if permitted(identity):
        files["evidence/source-identity.json"] = identity

    # The notice is held in-memory during packaging; no scratch file is needed.
    files["evidence/clock-loss/superseded-initial/NOTICE.json"] = None  # type: ignore[assignment]
    for name, path in list(files.items()):
        if path is None:
            continue
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"not a safe regular-file source: {name}")
    return files


def remove_plot_arrays(value, path: str, removed: list[dict]):
    """Drop only the declared visualization arrays; preserve all inference data."""
    if isinstance(value, dict):
        output = {}
        for key, child in value.items():
            child_path = f"{path}.{key}" if path else key
            if key in {"profile_grid_hz", "profile_scores"}:
                if not isinstance(child, list):
                    raise ValueError(f"expected plot array at {child_path}")
                removed.append({"json_path": child_path, "array_length": len(child)})
                continue
            output[key] = remove_plot_arrays(child, child_path, removed)
        return output
    if isinstance(value, list):
        return [remove_plot_arrays(child, f"{path}[{index}]", removed) for index, child in enumerate(value)]
    return value


def materialize_bytes(files: dict[str, Path]) -> tuple[dict[str, bytes], list[dict]]:
    payloads: dict[str, bytes] = {}
    transforms: list[dict] = []
    for name, path in files.items():
        if path is None:
            payloads[name] = json.dumps(
                {
                    "status": "superseded historical result; retained for audit only",
                    "superseded_files": [
                        "central-e6-b0.superseded.json", "central-e6-b0.superseded.log"
                    ],
                    "current_result": "../all-public-v2.json",
                    "reason": (
                        "The initial clock-grid probe incorrectly limited absolute retiming. "
                        "Its result is retained for audit only; it is superseded by all-public-v2."
                    ),
                }, indent=2, sort_keys=True
            ).encode() + b"\n"
            continue
        original = path.read_bytes()
        if path.suffix.lower() == ".json":
            value = json.loads(original)
            removed: list[dict] = []
            compact = remove_plot_arrays(value, "", removed)
            if removed:
                archived = (json.dumps(compact, indent=2, sort_keys=True, allow_nan=False) + "\n").encode()
                payloads[name] = archived
                transforms.append({
                    "archive_path": name,
                    "original_path": path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path),
                    "original_bytes": len(original),
                    "original_sha256": hashlib.sha256(original).hexdigest(),
                    "archived_bytes": len(archived),
                    "archived_sha256": hashlib.sha256(archived).hexdigest(),
                    "removed_json_arrays": removed,
                    "transform": "recursively remove only profile_grid_hz and profile_scores arrays",
                })
                continue
        payloads[name] = original
    return payloads, transforms


def safe_member(name: str) -> bool:
    p = PurePosixPath(name)
    return bool(name) and not p.is_absolute() and ".." not in p.parts and "\\" not in name


def package() -> dict:
    if ARCHIVE.exists() or MANIFEST.exists() or INVENTORY.exists() or SUMS.exists():
        raise FileExistsError("refusing to overwrite an existing Helper/GPU evidence package")
    inventory = raw_inventory()
    identity_check = source_identity_check()
    if not identity_check["ok"]:
        raise ValueError("frozen runtime source snapshots do not match source identity: " + json.dumps(identity_check))
    files = archive_files()
    payloads, transforms = materialize_bytes(files)
    # Manifest paths and member names are portable, safe, unique regular files.
    if len(payloads) != len(set(payloads)) or not all(safe_member(p) for p in payloads):
        raise ValueError("duplicate or unsafe archive path")
    payloads["evidence/raw-input-inventory.json"] = (
        json.dumps(inventory, indent=2, sort_keys=True, allow_nan=False) + "\n"
    ).encode()
    transform_notes = {
        "schema": "triad-helper-gpu-json-compaction-v1",
        "purpose": "remove large pure-plot profiles while preserving all inference candidates, scores, gates, costs and provenance",
        "transformed_file_count": len(transforms),
        "transforms": transforms,
    }
    payloads["evidence/json-compaction-notes.json"] = (
        json.dumps(transform_notes, indent=2, sort_keys=True, allow_nan=False) + "\n"
    ).encode()

    member_entries = [
        {"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        for name, data in sorted(payloads.items())
    ]
    # This checksum list covers all evidence members and intentionally omits
    # itself to avoid a recursive hash definition.
    sums_text = "".join(f"{item['sha256']}  {item['path']}\n" for item in member_entries)
    payloads["SHA256SUMS"] = sums_text.encode()
    member_entries.append({
        "path": "SHA256SUMS", "bytes": len(payloads["SHA256SUMS"]),
        "sha256": hashlib.sha256(payloads["SHA256SUMS"]).hexdigest(),
    })
    member_entries.sort(key=lambda item: item["path"])

    HERE.mkdir(parents=True, exist_ok=True)
    write_json(INVENTORY, inventory)
    with ARCHIVE.open("xb") as raw:
        with gzip.GzipFile(filename="", fileobj=raw, mode="wb", mtime=0, compresslevel=6) as gz:
            with tarfile.open(fileobj=gz, mode="w") as tar:
                for item in member_entries:
                    data = payloads[item["path"]]
                    info = tarfile.TarInfo(item["path"])
                    info.size = len(data)
                    info.mode = 0o644
                    info.mtime = info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    tar.addfile(info, io.BytesIO(data))

    manifest = {
        "schema": "triad-helper-gpu-evidence-manifest-v1",
        "archive": ARCHIVE.name,
        "archive_bytes": ARCHIVE.stat().st_size,
        "archive_sha256": sha256(ARCHIVE),
        "source_head_at_packaging": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True
        ).strip(),
        "source_identity": "evidence/source-identity.json",
        "source_identity_check": identity_check,
        "raw_input_inventory": INVENTORY.name,
        "raw_input_inventory_sha256": sha256(INVENTORY),
        "raw_input_count": inventory["entry_count"],
        "raw_input_total_bytes": inventory["total_bytes"],
        "raw_array_count": inventory["raw_array_count"],
        "transformed_json_count": len(transforms),
        "transform_notes": "evidence/json-compaction-notes.json",
        "entry_count": len(member_entries),
        "entries": member_entries,
        "scope": (
            "Sanitized HF full-GPU/forecast and FFT ladder results; HF physical validation "
            "summaries and manifests; old/new ramp manifests, truth and result JSON; clock-loss "
            "results (the initial central-only estimate is explicitly superseded); scripts, logs, "
            "source/test snapshots and source identities supplied under local-evidence. Native "
            "captures/workspace arrays are hash-inventoried but excluded. No cloud account, billing, "
            "SSH, resource-ID or temporary cloud metadata is included."
        ),
    }
    write_json(MANIFEST, manifest)
    sums = f"{manifest['archive_sha256']}  {ARCHIVE.name}\n{sha256(MANIFEST)}  {MANIFEST.name}\n{sha256(INVENTORY)}  {INVENTORY.name}\n"
    SUMS.write_text(sums)
    return manifest


def verify(manifest: dict) -> dict:
    seen = set()
    with tarfile.open(ARCHIVE, mode="r:gz") as tar:
        members = tar.getmembers()
        if len(members) != len(manifest["entries"]):
            raise ValueError("archive member count mismatch")
        for member, expected in zip(members, manifest["entries"], strict=True):
            if not member.isfile() or member.name != expected["path"] or member.name in seen:
                raise ValueError("archive has non-regular, duplicate or unexpected path")
            if not safe_member(member.name):
                raise ValueError("unsafe archive path: " + member.name)
            seen.add(member.name)
            stream = tar.extractfile(member)
            if stream is None:
                raise ValueError("unreadable archive member: " + member.name)
            checksum, size = digest_stream(stream)
            if size != expected["bytes"] or checksum != expected["sha256"]:
                raise ValueError("member hash/size mismatch: " + member.name)
            lowered = member.name.lower()
            if "triad-helper-gpu-cloud" in lowered or "/tmp/" in lowered:
                raise ValueError("forbidden cloud/tmp path leaked into archive")
    if sha256(ARCHIVE) != manifest["archive_sha256"] or ARCHIVE.stat().st_size != manifest["archive_bytes"]:
        raise ValueError("archive hash/size mismatch")
    for path in (MANIFEST, INVENTORY, SUMS):
        if not path.is_file() or path.is_symlink():
            raise ValueError("missing or unsafe sidecar: " + path.name)
    return {"verified_entries": len(seen), "archive_bytes": ARCHIVE.stat().st_size,
            "archive_sha256": sha256(ARCHIVE)}


def dry_run() -> dict:
    inventory = raw_inventory()
    identity_check = source_identity_check()
    files = archive_files()
    payloads, transforms = materialize_bytes(files)
    payloads["evidence/raw-input-inventory.json"] = (
        json.dumps(inventory, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n"
    ).encode()
    payloads["evidence/json-compaction-notes.json"] = (
        json.dumps({"transformed_file_count": len(transforms), "transforms": transforms},
                   sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n"
    ).encode()
    uncompressed = sum(len(data) for data in payloads.values())
    compressed = io.BytesIO()
    with gzip.GzipFile(filename="", fileobj=compressed, mode="wb", mtime=0, compresslevel=6) as gz:
        with tarfile.open(fileobj=gz, mode="w") as tar:
            for name, data in sorted(payloads.items()):
                info = tarfile.TarInfo(name)
                info.size = len(data)
                info.mode = 0o644
                info.mtime = info.uid = info.gid = 0
                info.uname = info.gname = ""
                tar.addfile(info, io.BytesIO(data))
    return {
        "evidence_files": len(payloads),
        "evidence_bytes_uncompressed": uncompressed,
        "archive_bytes_estimate_without_manifest": len(compressed.getvalue()),
        "raw_input_count": inventory["entry_count"],
        "raw_input_bytes": inventory["total_bytes"],
        "raw_array_count": inventory["raw_array_count"],
        "transformed_json_count": len(transforms),
        "source_identity_check": identity_check,
        "largest_evidence_sources": sorted(
            ((len(data), name) for name, data in payloads.items()), reverse=True
        )[:12],
        "archive_created": False,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--dry-run", action="store_true", help="estimate evidence package; write nothing")
    mode.add_argument("--release", action="store_true", help="freeze archive once after release signal")
    args = parser.parse_args()
    if args.dry_run:
        result = dry_run()
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        manifest = package()
        print(json.dumps({k: v for k, v in manifest.items() if k != "entries"}, indent=2, sort_keys=True))
        print(json.dumps(verify(manifest), sort_keys=True))

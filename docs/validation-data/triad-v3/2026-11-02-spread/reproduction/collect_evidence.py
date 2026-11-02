#!/usr/bin/env python3
"""Compact a completed local scientific evidence tree without changing it.

--source DIR --output FILE.tar.gz --completed-source
[--keep-workspace RELATIVE_DIR ...] [--raw-budget-mib 64]
[--receipt RELATIVE_FILE ...] [--inventory-dir OUTSIDE_SOURCE_DIR]

All raw arrays, including omitted arrays and failed-export orphans, are hashed.
Metadata/logs/command receipts are retained. Inventories are embedded in the tar
and optionally published separately. This tool does not qualify the science or
independently establish that jobs have finished; the caller must establish that.
"""
import argparse
from dataclasses import dataclass
from decimal import Decimal
import gzip
import hashlib
import io
import json
import math
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import tarfile
import tempfile


RAW_SUFFIXES = {".c128", ".f64", ".u64"}
METADATA_SUFFIXES = {".json", ".jsonl", ".npz", ".log", ".txt", ".md", ".csv", ".tsv",
                     ".yaml", ".yml", ".sh", ".py", ".sha256", ".out", ".err", ".stdout", ".stderr"}
PARTIAL_SUFFIXES = {".tmp", ".part", ".partial", ".pending", ".inprogress"}
FORBIDDEN_COMPONENTS = {".ssh", ".aws", ".azure", ".runpod", ".config", ".codex", ".git",
                        "credentials", "secrets", "private", "provider", "billing", "runpod"}
RESERVED = {"full-array-inventory.json", "full-source-file-inventory.json"}
BLOCK = 1024 * 1024


class EvidenceError(ValueError):
    pass


def require(ok, message):
    if not ok:
        raise EvidenceError(message)


def safe_relative(value):
    path = PurePosixPath(value)
    require(value and not path.is_absolute() and str(path) == value and all(p not in (".", "..") for p in path.parts),
            f"expected canonical relative path: {value!r}")
    require(not any("\\" in p or ":" in p or any(ord(c) < 32 for c in p) for p in path.parts),
            f"unsafe archive path: {value!r}")
    for part in path.parts:
        lower = part.lower()
        require(lower not in FORBIDDEN_COMPONENTS and lower not in {"id_rsa", "id_ed25519", ".env"}
                and not lower.startswith(("runpod-", "runpod_", "provider-", "provider_", "credentials.",
                                          "credentials-", "ssh-", "ssh_", "billing-", ".env."))
                and not lower.endswith((".pem", ".key")), f"provider/private path prohibited: {value}")
    return path


def signature(s):
    # atime is intentionally absent: reading evidence may update it.
    return (s.st_dev, s.st_ino, s.st_mode, s.st_size, s.st_mtime_ns, s.st_ctime_ns)


@dataclass(frozen=True)
class Entry:
    path: Path
    signature: tuple


def scan(source):
    files = {}; directories = {}
    def visit(directory, prefix):
        directories[prefix] = signature(directory.stat(follow_symlinks=False))
        with os.scandir(directory) as listing:
            children = sorted(listing, key=lambda x: x.name)
        for child in children:
            name = f"{prefix}/{child.name}" if prefix else child.name
            safe_relative(name)
            require(name not in RESERVED, f"source collides with generated inventory: {name}")
            info = child.stat(follow_symlinks=False)
            require(not stat.S_ISLNK(info.st_mode), f"symlink prohibited: {name}")
            if stat.S_ISDIR(info.st_mode):
                visit(Path(child.path), name)
            elif stat.S_ISREG(info.st_mode):
                files[name] = Entry(Path(child.path), signature(info))
            else:
                raise EvidenceError(f"nonregular source object prohibited: {name}")
    visit(source, "")
    return files, directories


def checked_open(entry):
    descriptor = os.open(entry.path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    stream = os.fdopen(descriptor, "rb")
    try:
        require(signature(os.fstat(stream.fileno())) == entry.signature, f"source changed before read: {entry.path.name}")
        return stream
    except BaseException:
        stream.close()
        raise


def checked_close_state(stream, entry):
    require(signature(os.fstat(stream.fileno())) == entry.signature, f"source changed while reading: {entry.path.name}")


def hash_entry(entry):
    digest = hashlib.sha256()
    with checked_open(entry) as stream:
        for data in iter(lambda: stream.read(BLOCK), b""):
            digest.update(data)
        checked_close_state(stream, entry)
    return digest.hexdigest()


def logical_suffix(name):
    path = PurePosixPath(name)
    suffix = path.suffix.lower()
    partial = suffix in PARTIAL_SUFFIXES
    return (PurePosixPath(path.stem).suffix.lower() if partial else suffix), partial


def strict_json(stream, lines=False):
    def nonfinite(value):
        raise EvidenceError(f"nonstandard JSON numeric constant: {value}")
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, f"duplicate JSON key: {key}")
            result[key] = value
        return result
    kwargs = dict(parse_constant=nonfinite, object_pairs_hook=unique, parse_float=Decimal)
    if lines:
        for line in stream:
            if line.strip(): json.loads(line, **kwargs)
        return None
    return json.load(stream, **kwargs)


def json_bytes(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False)+"\n").encode()


def inspect(source, files, keep, receipts, budget):
    manifests = {}; declared = {}; size_checks = []; metadata = {}; records = []
    for name, entry in sorted(files.items()):
        suffix, partial = logical_suffix(name)
        if suffix in (".json", ".jsonl"):
            try:
                with checked_open(entry) as stream:
                    document = strict_json(stream, suffix == ".jsonl")
                    checked_close_state(stream, entry)
            except (ValueError, UnicodeError) as error:
                raise EvidenceError(f"invalid JSON, possibly unfinished: {name}: {error}") from error
            if PurePosixPath(name).name == "manifest.json" and isinstance(document, dict) and document.get("format") == "triad-block-stats-v1":
                workspace = str(PurePosixPath(name).parent)
                workspace = "" if workspace == "." else workspace
                incomplete = document.get("complete") is False or document.get("status") in {"error", "failed", "partial", "incomplete"}
                manifests[workspace] = "explicitly_incomplete_manifest" if incomplete else "completed_manifest"
                sizes = document.get("file_sizes", {})
                require(isinstance(sizes, dict), f"invalid manifest file_sizes: {name}")
                for filename, expected in sorted(sizes.items()):
                    require(isinstance(filename, str) and PurePosixPath(filename).name == filename, f"invalid declared filename: {name}")
                    safe_relative(filename)
                    require(type(expected) is int and expected >= 0, f"invalid declared file size: {name}: {filename}")
                    target = f"{workspace}/{filename}" if workspace else filename
                    actual = files[target].signature[3] if target in files else None
                    matched = actual == expected
                    require(matched or incomplete, f"completed manifest file_sizes mismatch: {target}: declared {expected}, actual {actual}")
                    declared[target] = {"manifest": name, "bytes": expected, "verified": matched}
                    size_checks.append({"file": target, "declared_bytes": expected, "actual_bytes": actual,
                                        "matched": matched, "manifest_incomplete": incomplete})
        is_raw = suffix in RAW_SUFFIXES
        is_metadata = suffix in METADATA_SUFFIXES or PurePosixPath(name).name.lower() in {"stdout", "stderr"}
        require(is_raw or is_metadata or name in receipts, f"unrecognized nonraw evidence type; declare --receipt explicitly: {name}")
        require(not (is_raw and name in receipts), f"--receipt cannot bypass the raw budget: {name}")
        workspace = str(PurePosixPath(name).parent)
        workspace = "" if workspace == "." else workspace
        retain = not is_raw or workspace in keep
        records.append({"path": name, "bytes": entry.signature[3], "kind": "raw_array" if is_raw else "metadata_or_receipt",
                        "sha256": hash_entry(entry), "retained": retain, "omitted": not retain,
                        "partial_filename": partial,
                        "retention_reason": "explicit_workspace_selection" if is_raw and retain else "raw_workspace_not_selected" if is_raw else "all_metadata_logs_and_receipts"})
        metadata[name] = (workspace, is_raw)
    selected_raw = sum(r["bytes"] for r in records if r["kind"] == "raw_array" and r["retained"])
    require(selected_raw <= budget, f"selected raw arrays exceed budget: {selected_raw} > {budget} bytes")
    for workspace in keep:
        require(any(raw and parent == workspace for parent, raw in metadata.values()), f"selected workspace has no direct raw arrays: {workspace}")
    arrays = []
    for record in records:
        if record["kind"] != "raw_array": continue
        workspace = metadata[record["path"]][0]
        state = manifests.get(workspace, "orphan_without_manifest_incomplete_or_undeclared")
        arrays.append({**record, "workspace": workspace,
                       "workspace_state": state, "declared_manifest_size": declared.get(record["path"]),
                       "incomplete_or_undeclared": state != "completed_manifest" or record["path"] not in declared or record["partial_filename"]})
    return records, arrays, size_checks, selected_raw


class HashingReader:
    def __init__(self, stream):
        self.stream = stream; self.digest = hashlib.sha256(); self.bytes = 0
    def read(self, size=-1):
        data = self.stream.read(size)
        self.digest.update(data); self.bytes += len(data)
        return data


def tar_info(name, size=0, directory=False):
    safe_relative(name)
    info = tarfile.TarInfo(name + "/" if directory else name)
    info.size = size; info.mtime = 0; info.mode = 0o755 if directory else 0o644
    info.uid = info.gid = 0; info.uname = info.gname = ""
    if directory: info.type = tarfile.DIRTYPE
    return info


def write_archive(path, files, directories, records, inventories):
    retained = {r["path"]: r for r in records if r["retained"]}
    members = sorted(set(retained) | set(inventories) | {d for d in directories if d})
    with path.open("wb") as output:
        with gzip.GzipFile(fileobj=output, mode="wb", filename="", mtime=0, compresslevel=6) as compressed:
            with tarfile.open(fileobj=compressed, mode="w|", format=tarfile.PAX_FORMAT) as archive:
                for name in members:
                    if name in directories:
                        archive.addfile(tar_info(name, directory=True))
                    elif name in inventories:
                        data = inventories[name]; archive.addfile(tar_info(name, len(data)), io.BytesIO(data))
                    else:
                        entry = files[name]
                        with checked_open(entry) as stream:
                            reader = HashingReader(stream)
                            archive.addfile(tar_info(name, retained[name]["bytes"]), reader)
                            checked_close_state(stream, entry)
                            require(reader.bytes == retained[name]["bytes"] and reader.digest.hexdigest() == retained[name]["sha256"],
                                    f"source changed between hashing and copying: {name}")
        output.flush(); os.fsync(output.fileno())


def collect(source, output, *, keep_workspaces=(), raw_budget_mib=64, receipts=(), completed_source=False, inventory_dir=None):
    require(completed_source, "only use after jobs finish; explicit --completed-source is required")
    source = Path(source).absolute(); output = Path(output).absolute()
    require(source.is_dir() and not source.is_symlink(), "source must be an existing nonsymlink directory")
    source = source.resolve()
    require(not output.resolve().is_relative_to(source), "archive output must be outside source")
    require(not output.exists() and not output.is_symlink(), "archive output already exists")
    require(isinstance(raw_budget_mib, (int, float)) and not isinstance(raw_budget_mib, bool)
            and math.isfinite(raw_budget_mib) and raw_budget_mib >= 0, "invalid raw budget")
    budget = int(Decimal(str(raw_budget_mib))*1024*1024)
    keep = {str(safe_relative(v)) for v in keep_workspaces}; selected_receipts = {str(safe_relative(v)) for v in receipts}
    require(len(keep) == len(keep_workspaces) and len(selected_receipts) == len(receipts), "duplicate selection argument")
    sidecar = Path(inventory_dir).absolute() if inventory_dir is not None else None
    if sidecar is not None:
        require(not sidecar.resolve().is_relative_to(source) and sidecar.resolve() != output.resolve(), "inventory directory must be outside source and distinct from archive")
        require(not sidecar.exists() and not sidecar.is_symlink(), "inventory directory already exists")
    files, directories = scan(source)
    require(files, "scientific source contains no files")
    require(keep <= set(directories), "selected workspace directory is missing")
    require(selected_receipts <= set(files), "selected receipt file is missing")
    records, arrays, size_checks, selected_bytes = inspect(source, files, keep, selected_receipts, budget)
    common = {"collector_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "completed_source_asserted_by_caller": True, "raw_budget_bytes": budget,
              "selected_raw_bytes": selected_bytes, "selected_workspaces": sorted(keep),
              "scope": "File integrity/compaction only, not scientific qualification or an independent job-completion check. All raw files are hashed, including omitted and orphan/partial arrays. Original metadata/log/receipt bytes are preserved, including embedded technical command paths. Archive member names are relative; known private/provider paths and symlinks are rejected. No general secret-content scan or redaction is claimed.",
              "snapshot_scope": "File identities, sizes and nanosecond modification/change times checked before/after reads and after archive construction; retained content rehashed during copying. No atomic filesystem snapshot or post-publication immutability claim."}
    inventories = {
        "full-array-inventory.json": json_bytes({"schema": "triad-full-array-inventory-v1", **common, "arrays": arrays,
            "summary": {"raw_files": len(arrays), "raw_bytes": sum(x["bytes"] for x in arrays),
                        "retained_raw_files": sum(x["retained"] for x in arrays),
                        "omitted_raw_files": sum(x["omitted"] for x in arrays)}}),
        "full-source-file-inventory.json": json_bytes({"schema": "triad-full-source-file-inventory-v1", **common,
            "directories": sorted(d for d in directories if d), "files": records, "manifest_size_checks": size_checks,
            "summary": {"files": len(records), "source_bytes": sum(x["bytes"] for x in records),
                        "retained_files": sum(x["retained"] for x in records), "omitted_files": sum(x["omitted"] for x in records)}})}
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(prefix="."+output.name+".partial-", dir=output.parent); os.close(fd)
    temporary = Path(temporary_name); temp_sidecar = None; published_archive = False; published_sidecar = False
    try:
        write_archive(temporary, files, directories, records, inventories)
        final_files, final_directories = scan(source)
        require({k: v.signature for k, v in final_files.items()} == {k: v.signature for k, v in files.items()}
                and final_directories == directories, "source tree changed during compaction; archive not published")
        if sidecar is not None:
            sidecar.parent.mkdir(parents=True, exist_ok=True)
            temp_sidecar = Path(tempfile.mkdtemp(prefix="."+sidecar.name+".partial-", dir=sidecar.parent))
            for name, data in inventories.items(): (temp_sidecar/name).write_bytes(data)
            sidecar.mkdir()  # Exclusive creation; do not replace another actor's directory.
            published_sidecar = True
            for name in inventories: os.link(temp_sidecar/name, sidecar/name)
            shutil.rmtree(temp_sidecar); temp_sidecar = None
        os.link(temporary, output)  # Exclusive publication: never replace an existing archive.
        published_archive = True
        return {"archive": str(output), "archive_bytes": output.stat().st_size,
                "archive_sha256": hash_entry(Entry(output, signature(output.stat()))),
                "source_files": len(records), "raw_files_hashed": len(arrays), "raw_bytes_hashed": sum(x["bytes"] for x in arrays),
                "retained_raw_bytes": selected_bytes, "omitted_raw_files": sum(x["omitted"] for x in arrays),
                "scientific_qualification": False}
    except BaseException:
        if published_archive: output.unlink()
        if published_sidecar: shutil.rmtree(sidecar)
        raise
    finally:
        temporary.unlink(missing_ok=True)
        if temp_sidecar is not None: shutil.rmtree(temp_sidecar)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path); parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--completed-source", action="store_true")
    parser.add_argument("--keep-workspace", action="append", default=[])
    parser.add_argument("--raw-budget-mib", type=float, default=64)
    parser.add_argument("--receipt", action="append", default=[]); parser.add_argument("--inventory-dir", type=Path)
    args = parser.parse_args()
    try:
        result = collect(args.source, args.output, keep_workspaces=args.keep_workspace, raw_budget_mib=args.raw_budget_mib,
                         receipts=args.receipt, completed_source=args.completed_source, inventory_dir=args.inventory_dir)
    except (EvidenceError, OSError) as error:
        parser.exit(1, f"evidence compaction failed: {error}\n")
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()

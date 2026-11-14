#!/usr/bin/env python3
"""Bundle the canonical native CUDA source closure plus the contained appraisal.

No credentials, captures, build trees, caches or account records are selected.
The canonical tools/triad_gpu_bundle.py remains the authority for native and
pinned third-party inputs. This wrapper only adds the appraisal's maintained
sources, tests, scenario manifests, scripts and documentation.

Create: python3 cloud-bundle.py --output local-evidence/appraisal-source.tar.gz
Check an extracted bundle: python3 cloud-bundle.py --verify-source
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath
import stat
import sys
import tarfile

sys.dont_write_bytecode = True
APPRAISAL = "_appraisal/highFidelity-triple_Robust_Modem"
FORMAT = "datapump.triad.high-fidelity-source.v1"
MANIFEST = "BUNDLE-MANIFEST.json"
TOP_SUFFIXES = {".md", ".py", ".sh"}
SUBDIRECTORIES = {
    "include": {".hpp", ".h"},
    "src": {".cpp", ".hpp", ".h", ".cu"},
    "cmake": {".cmake"},
    "tests": {".cpp", ".hpp", ".py"},
    "campaigns": {".json"},
}


def native_module(root: Path):
    path = root / "tools/triad_gpu_bundle.py"
    spec = importlib.util.spec_from_file_location("_native_triad_bundle", path)
    if spec is None or spec.loader is None:
        raise ValueError("canonical native bundle helper is unavailable")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def collect(root_fd: int, native) -> list[tuple[str, os.stat_result, str | None]]:
    selected = {rel: (info, expected) for rel, info, expected in native._collect(root_fd)}
    prefix = PurePosixPath(APPRAISAL)
    with native._open_directory(prefix, dir_fd=root_fd) as directory:
        for name in sorted(os.listdir(directory)):
            rel = f"{APPRAISAL}/{name}"
            if not native._eligible(rel):
                continue
            info = os.stat(name, dir_fd=directory, follow_symlinks=False)
            if stat.S_ISLNK(info.st_mode):
                # A selected-looking link is an error, not a source redirect.
                if name == "CMakeLists.txt" or Path(name).suffix in TOP_SUFFIXES:
                    raise native.BundleError(f"appraisal input must not be a symlink: {rel}")
                continue
            if stat.S_ISREG(info.st_mode) and (name == "CMakeLists.txt" or Path(name).suffix in TOP_SUFFIXES):
                selected[rel] = (info, None)
        for name, suffixes in SUBDIRECTORIES.items():
            with native._open_directory(PurePosixPath(name), dir_fd=directory) as child:
                for rel, info in native._walk_sources(child, f"{APPRAISAL}/{name}", suffixes):
                    selected[rel] = (info, None)
    required = {
        f"{APPRAISAL}/CMakeLists.txt", f"{APPRAISAL}/cmake/register.cmake",
        f"{APPRAISAL}/src/main.cpp", f"{APPRAISAL}/src/channel.cpp",
        f"{APPRAISAL}/src/scenarios.cpp", f"{APPRAISAL}/include/appraisal/channel.hpp",
        f"{APPRAISAL}/run.py", f"{APPRAISAL}/cloud-bundle.py", f"{APPRAISAL}/cloud-build.sh",
    }
    absent = sorted(required - selected.keys())
    if absent:
        raise native.BundleError("appraisal closure is incomplete: " + ", ".join(absent))
    return [(rel, *selected[rel]) for rel in sorted(selected)]


def read_inputs(root: Path, native) -> tuple[list[dict], list[tuple[str, bytes]]]:
    records, payloads = [], []
    with native._open_directory(root) as root_fd:
        for rel, info, expected in collect(root_fd, native):
            data = native._read_source(root_fd, rel, info)
            digest = hashlib.sha256(data).hexdigest()
            if expected is not None and digest != expected:
                raise native.BundleError(f"modified pinned third-party source: {rel}")
            records.append({"path": rel, "size": len(data), "sha256": digest})
            payloads.append((rel, data))
    return records, payloads


def create_bundle(root: Path, output: Path) -> dict:
    native = native_module(root)
    if output.is_symlink():
        raise native.BundleError("output must not be a symlink")
    output = output.parent.resolve(strict=True) / output.name
    if output.exists():
        raise native.BundleError(f"output already exists: {output}")
    if not output.name.endswith((".tar.gz", ".tgz")):
        raise native.BundleError("output must end in .tar.gz or .tgz")
    if root in output.parents:
        allowed = root / APPRAISAL / "local-evidence"
        if allowed not in output.parents:
            raise native.BundleError("an in-repository bundle belongs only in the appraisal's ignored local-evidence/")
    records, payloads = read_inputs(root, native)
    manifest = {"format": FORMAT, "native_allowlist": "tools/triad_gpu_bundle.py", "files": records}
    manifest_bytes = (json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n").encode()
    raw = io.BytesIO()
    with gzip.GzipFile(fileobj=raw, mode="wb", filename="", mtime=0, compresslevel=9) as compressed:
        with tarfile.open(fileobj=compressed, mode="w", format=tarfile.USTAR_FORMAT) as archive:
            for rel, data in payloads + [(MANIFEST, manifest_bytes)]:
                entry = tarfile.TarInfo(native._safe_rel(rel))
                entry.size = len(data)
                entry.mode = 0o755 if rel.endswith(".sh") else 0o644
                entry.uid = entry.gid = entry.mtime = 0
                entry.uname = entry.gname = ""
                archive.addfile(entry, io.BytesIO(data))
    data = raw.getvalue()
    descriptor = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
    except BaseException:
        output.unlink(missing_ok=True)
        raise
    return {"format": FORMAT, "path": str(output), "compressed_bytes": len(data),
            "uncompressed_bytes": sum(r["size"] for r in records), "file_count": len(records),
            "sha256": hashlib.sha256(data).hexdigest(),
            "manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest()}


def verify_source(root: Path) -> dict:
    native = native_module(root)
    with native._open_directory(root) as root_fd:
        info = native._inventory_file(root_fd, MANIFEST)
        manifest_bytes = native._read_source(root_fd, MANIFEST, info)
    manifest = json.loads(manifest_bytes)
    if manifest.get("format") != FORMAT:
        raise native.BundleError("not a high-fidelity source manifest")
    records, _ = read_inputs(root, native)
    if manifest.get("files") != records:
        raise native.BundleError("extracted source differs from the frozen bundle manifest; rebuild the source bundle")
    return {"format": FORMAT, "verified": True, "file_count": len(records),
            "manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    operation = parser.add_mutually_exclusive_group(required=True)
    operation.add_argument("--output", type=Path)
    operation.add_argument("--verify-source", action="store_true")
    args = parser.parse_args()
    root = args.root.absolute()
    try:
        result = verify_source(root) if args.verify_source else create_bundle(root, args.output)
        print(json.dumps(result, sort_keys=True))
    except (OSError, ValueError, RuntimeError) as error:
        print(f"cloud-bundle: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

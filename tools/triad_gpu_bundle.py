#!/usr/bin/env python3
"""Create a deterministic source archive for the compiled Robust GPU probe.

The input set is intentionally allowlisted: native project sources and headers,
the CMake build description, the native probe and its orchestration scripts,
and the pinned XZ and qrcodegen sources. LibTorch, CUDA and system build
dependencies come from the remote build environment.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import gzip
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys
import tarfile


_TOP_FILES = ("CMakeLists.txt", "CMakePresets.json", "LICENSE", "build.sh", "_tests-RobustModem.md")
_NATIVE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx",
                    ".inc", ".inl", ".s", ".cu", ".cuh"}
_XZ_INVENTORY = "third_party/xz/UPSTREAM.sha256"
_XZ_BUILD_METADATA = {
    "AUTHORS", "CMakeLists.txt", "COPYING", "COPYING.0BSD", "COPYING.LGPLv2.1", "INSTALL", "README",
    "src/common/common_w32res.rc", "src/common/w32_application.manifest",
    "src/common/w32_application.manifest.comments.txt", "src/liblzma/liblzma.pc.in",
    "src/liblzma/liblzma_generic.map", "src/liblzma/liblzma_linux.map",
    "src/liblzma/liblzma_w32res.rc", "src/liblzma/validate_map.sh",
}
_PROBE_TOOLS = (
    "triad-receiver-probe.cpp", "triad_probe_json.hpp", "triad_probe_spool.hpp",
    "triad-helper-export.cpp", "triad_fixture_key.hpp", "triad-audio-stream.cpp",
    "triad_native.py", "triad_cases.py", "triad_gpu_workload.py", "triad_gpu_bundle.py",
    "triad_gpu_cpu_forecast.py", "triad_case_metrics.py", "triad_case_capacity.py",
)
_EXACT_FILES = (*_TOP_FILES, "src/pump.manifest", "tools/build_capacity.py",
                *("tools/" + name for name in _PROBE_TOOLS),
                "tools/diagnostic_iq_noise_options.hpp", "tests/test_triad_tensor.cpp",
                "third_party/xz/README.datapump.md",
                "third_party/qrcodegen/LICENSE", "third_party/qrcodegen/qrcodegen.cpp",
                "third_party/qrcodegen/qrcodegen.hpp")
_SECRET_NAME = re.compile(r"(?:secret|credential|password|token|private.?key|id_(?:rsa|ed25519))", re.I)
_SECRET_SUFFIXES = {".pem", ".key", ".p12", ".pfx"}
_PRUNE_DIRS = {"build", "build-debug", "build-release", "local-evidence", "env", "venv",
               ".venv", "__pycache__", "node_modules", "site-packages", ".cache"}
_GENERATED_DIR = re.compile(r"(?:^|[-_.])(?:build|env|venv|virtualenv)(?:$|[-_.])", re.I)


class BundleError(ValueError):
    """Unsafe or incomplete bundle input/output."""


def _safe_rel(path: str) -> str:
    if any(part in ("", ".", "..") for part in path.split("/")) or "\\" in path or "\x00" in path:
        raise BundleError(f"unsafe archive path: {path!r}")
    return path


def _eligible(rel: str) -> bool:
    parts = PurePosixPath(rel).parts
    name = parts[-1]
    return (not any(p.startswith(".") for p in parts)
            and not _SECRET_NAME.search(name)
            and Path(name).suffix.lower() not in _SECRET_SUFFIXES)


def _source_directory(name: str) -> bool:
    return (not name.startswith(".") and name.casefold() not in _PRUNE_DIRS
            and not _GENERATED_DIR.search(name) and not _SECRET_NAME.search(name))


@contextmanager
def _open_directory(path: PurePosixPath, *, dir_fd: int | None = None):
    """Open every component separately so ancestors cannot redirect a read."""
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
    parts = path.parts
    if dir_fd is None:
        fd = os.open(path.anchor, flags)
        parts = parts[1:]
    else:
        fd = os.dup(dir_fd)
    try:
        for part in parts:
            try:
                child = os.open(part, flags, dir_fd=fd)
            except OSError as exc:
                raise BundleError(f"unsafe or unavailable source directory: {path}") from exc
            os.close(fd)
            fd = child
        yield fd
    finally:
        os.close(fd)


def _snapshot(info: os.stat_result) -> tuple[int, ...]:
    return (info.st_dev, info.st_ino, info.st_mode, info.st_nlink,
            info.st_size, info.st_mtime_ns, info.st_ctime_ns)


def _read_source(root_fd: int, rel: str, inventoried: os.stat_result) -> bytes:
    path = PurePosixPath(_safe_rel(rel))
    with _open_directory(path.parent, dir_fd=root_fd) as parent:
        try:
            fd = os.open(path.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=parent)
            with os.fdopen(fd, "rb") as source:
                before = os.fstat(source.fileno())
                if not stat.S_ISREG(before.st_mode) or _snapshot(before) != _snapshot(inventoried):
                    raise BundleError(f"source changed after inventory: {rel}")
                data = source.read(before.st_size + 1)
                after = os.fstat(source.fileno())
                current = os.stat(path.name, dir_fd=parent, follow_symlinks=False)
                if (len(data) != before.st_size or _snapshot(after) != _snapshot(before)
                        or _snapshot(current) != _snapshot(before)):
                    raise BundleError(f"source changed during read: {rel}")
        except OSError as exc:
            raise BundleError(f"unsafe or unavailable source file: {rel}") from exc
    return data


def _inventory_file(root_fd: int, rel: str) -> os.stat_result:
    path = PurePosixPath(_safe_rel(rel))
    with _open_directory(path.parent, dir_fd=root_fd) as parent:
        info = os.stat(path.name, dir_fd=parent, follow_symlinks=False)
    if not stat.S_ISREG(info.st_mode):
        raise BundleError(f"required source must be a regular file: {rel}")
    return info


def _walk_sources(directory: int, prefix: str, suffixes: set[str]):
    for name in sorted(os.listdir(directory)):
        rel = _safe_rel(f"{prefix}/{name}")
        if not _eligible(rel):
            continue
        info = os.stat(name, dir_fd=directory, follow_symlinks=False)
        if stat.S_ISDIR(info.st_mode) and _source_directory(name):
            with _open_directory(PurePosixPath(name), dir_fd=directory) as child:
                if not os.path.samestat(info, os.fstat(child)):
                    raise BundleError(f"source directory changed during inventory: {rel}")
                yield from _walk_sources(child, rel, suffixes)
        elif stat.S_ISREG(info.st_mode) and PurePosixPath(name).suffix.lower() in suffixes:
            yield rel, info


def _collect(root_fd: int) -> list[tuple[str, os.stat_result, str | None]]:
    selected = {rel: (_inventory_file(root_fd, rel), None) for rel in _EXACT_FILES}
    for rel_dir, suffixes in (("src", _NATIVE_SUFFIXES), ("include", _NATIVE_SUFFIXES), ("cmake", {".cmake"})):
        with _open_directory(PurePosixPath(rel_dir), dir_fd=root_fd) as directory:
            selected.update((rel, (info, None)) for rel, info in _walk_sources(directory, rel_dir, suffixes))

    # The native build verifies every member of the pinned upstream inventory,
    # including license and platform build metadata. Keep that exact closure.
    inventory_info = _inventory_file(root_fd, _XZ_INVENTORY)
    selected[_XZ_INVENTORY] = (inventory_info, None)
    for line in _read_source(root_fd, _XZ_INVENTORY, inventory_info).decode("ascii").splitlines():
        digest, separator, path = line.partition("  ")
        path = _safe_rel(path)
        rel = f"third_party/xz/{path}"
        if (not separator or not re.fullmatch(r"[0-9a-f]{64}", digest) or rel in selected
                or not _eligible(rel) or not all(_source_directory(part) for part in path.split("/")[:-1])
                or (PurePosixPath(path).suffix.lower() not in _NATIVE_SUFFIXES | {".cmake"}
                    and path not in _XZ_BUILD_METADATA)):
            raise BundleError(f"invalid or disallowed XZ inventory entry: {path!r}")
        selected[rel] = (_inventory_file(root_fd, rel), digest)

    required = {"cmake/VendoredLzma.cmake", "cmake/XzReceiveHardening.cmake",
                "cmake/TriadTensor.cmake", "third_party/xz/CMakeLists.txt",
                "src/triad_tensor_backend.cpp", "include/datapump/triad_acceleration.hpp"}
    absent = sorted(required - selected.keys())
    if absent:
        raise BundleError("source/build closure is incomplete; missing: " + ", ".join(absent))
    return [(rel, *selected[rel]) for rel in sorted(selected)]


def create_bundle(root: Path | str, destination: Path | str) -> dict[str, object]:
    root_arg = Path(root).absolute()
    root = Path("/" + os.path.abspath(root_arg).lstrip("/"))
    dest_arg = Path(destination)
    if dest_arg.is_symlink():
        raise BundleError("destination must not be a symlink")
    parent = dest_arg.parent.resolve(strict=True)
    dest = parent / dest_arg.name
    if dest.exists():
        raise BundleError(f"destination already exists: {dest}")
    if dest == root or root in dest.parents:
        raise BundleError("destination must be outside the source tree")
    if not dest.name.endswith((".tar.gz", ".tgz")):
        raise BundleError("destination must end in .tar.gz or .tgz")

    records = []
    payloads = []
    with _open_directory(root_arg) as root_fd:
        for rel, info, expected in _collect(root_fd):
            data = _read_source(root_fd, rel, info)
            digest = hashlib.sha256(data).hexdigest()
            if expected is not None and digest != expected:
                raise BundleError(f"modified pinned XZ source: {rel}")
            records.append({"path": rel, "size": len(data), "sha256": digest})
            payloads.append((rel, data))
    manifest = {"format": "datapump.triad.native-gpu-source.v1", "files": records}
    manifest_bytes = (json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n").encode()

    # Build to memory first so failures cannot leave a partial destination.
    raw = io.BytesIO()
    with gzip.GzipFile(fileobj=raw, mode="wb", filename="", mtime=0, compresslevel=9) as gz:
        with tarfile.open(fileobj=gz, mode="w", format=tarfile.USTAR_FORMAT) as tar:
            for rel, data in payloads + [("BUNDLE-MANIFEST.json", manifest_bytes)]:
                info = tarfile.TarInfo(_safe_rel(rel))
                info.size = len(data)
                info.mode = 0o755 if rel == "build.sh" else 0o644
                info.uid = info.gid = 0
                info.uname = info.gname = ""
                info.mtime = 0
                tar.addfile(info, io.BytesIO(data))
    archive = raw.getvalue()
    # Exclusive create preserves collision safety if another process races us.
    fd = os.open(dest, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(fd, "wb") as out:
            out.write(archive)
    except BaseException:
        try:
            dest.unlink()
        except OSError:
            pass
        raise
    return {"path": str(dest), "compressed_bytes": len(archive), "file_count": len(records),
            "uncompressed_bytes": sum(r["size"] for r in records),
            "manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest()}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        print(json.dumps(create_bundle(args.root, args.output), sort_keys=True))
    except (OSError, BundleError) as exc:
        print(f"triad_gpu_bundle: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

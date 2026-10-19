#!/usr/bin/env python3
"""Prepare an isolated, hash-gated source tree for one first-transition capture."""
from __future__ import annotations
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import tarfile
import tempfile

ALLOWED = {
    "src/live.cpp", "src/pattern_receiver.cpp", "src/audio_trace.hpp",
    "src/first_transition_probe.hpp",
}

def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def read_json(path: Path):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError(f"duplicate JSON key: {key}")
            result[key] = value
        return result
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=pairs)

def safe_relative(value: str) -> str:
    if not isinstance(value, str) or not value or "\\" in value:
        raise ValueError("path must be a nonempty POSIX relative path")
    p = PurePosixPath(value)
    if p.is_absolute() or any(part in ("", ".", "..") for part in p.parts):
        raise ValueError(f"unsafe relative path: {value!r}")
    return p.as_posix()

def git(root: Path, *args: str) -> bytes:
    return subprocess.run(["git", "-c", "safe.directory=" + str(root), "-C", str(root), *args], check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout

def tracked_tree(archive: bytes):
    files = {}
    with tarfile.open(fileobj=__import__("io").BytesIO(archive), mode="r:") as tf:
        for member in tf.getmembers():
            name = safe_relative(member.name)
            if member.isdir():
                continue
            if not member.isfile():
                raise ValueError(f"archive contains unsupported non-file entry: {name}")
            stream = tf.extractfile(member)
            if stream is None:
                raise ValueError(f"cannot read archived file: {name}")
            files[name] = stream.read()
    return files

def inventory(files):
    prefixes = ("src/", "include/", "tests/", "cmake/")
    selected = {name: sha(data) for name, data in files.items()
                if name.startswith(prefixes) or name == "CMakeLists.txt"}
    return dict(sorted(selected.items()))

def prepare(root: Path, overlay: Path, output: Path):
    root = root.resolve(strict=True)
    overlay = overlay.resolve(strict=True)
    output = output.absolute()
    if output.exists():
        raise FileExistsError(f"refusing to overwrite existing output: {output}")
    manifest_path = overlay / "manifest.json"
    manifest = read_json(manifest_path)
    if not isinstance(manifest, dict) or not {"baseline_source_commit", "files"} <= set(manifest):
        raise ValueError("manifest requires baseline_source_commit and files")
    commit = manifest["baseline_source_commit"]
    if not isinstance(commit, str) or len(commit) != 40 or any(c not in "0123456789abcdef" for c in commit):
        raise ValueError("baseline_source_commit must be a lowercase full Git SHA")
    head = git(root, "rev-parse", "HEAD").decode("ascii").strip()
    resolved = git(root, "rev-parse", commit + "^{commit}").decode("ascii").strip()
    if resolved != commit:
        raise ValueError("baseline commit did not resolve exactly")
    archive = git(root, "archive", "--format=tar", commit)
    base_files = tracked_tree(archive)
    entries = manifest["files"]
    if not isinstance(entries, dict) or not entries:
        raise ValueError("files must be a nonempty path-to-entry object")
    normalized = {}
    payloads = {}
    for path, entry in entries.items():
        path = safe_relative(path)
        if path not in ALLOWED:
            raise ValueError(f"overlay path is not allowed: {path}")
        if path in normalized or not isinstance(entry, dict):
            raise ValueError(f"duplicate or malformed entry for {path}")
        if set(entry) != {"before_sha256", "after_sha256", "payloadpath"}:
            raise ValueError(f"{path}: expected before_sha256, after_sha256, payloadpath")
        before, after = entry["before_sha256"], entry["after_sha256"]
        payload_name = safe_relative(entry["payloadpath"])
        if before is not None and (not isinstance(before, str) or len(before) != 64):
            raise ValueError(f"{path}: invalid before_sha256")
        if not isinstance(after, str) or len(after) != 64:
            raise ValueError(f"{path}: invalid after_sha256")
        payload_path = (overlay / payload_name).resolve(strict=True)
        if not payload_path.is_file() or not payload_path.is_relative_to(overlay):
            raise ValueError(f"{path}: payload must be a regular file inside overlay directory")
        payload = payload_path.read_bytes()
        if sha(payload) != after:
            raise ValueError(f"{path}: payload digest mismatch")
        base = base_files.get(path)
        checkout_path = root / path
        checkout = checkout_path.read_bytes() if checkout_path.is_file() else None
        if before is None:
            if base is not None or checkout is not None:
                raise ValueError(f"{path}: expected a new file absent from HEAD and checkout")
        else:
            if base is None or checkout is None or sha(base) != before or sha(checkout) != before:
                raise ValueError(f"{path}: before_sha256 does not match both HEAD and checkout")
        normalized[path] = {"before_sha256": before, "after_sha256": after,
                            "payloadpath": payload_name}
        payloads[path] = payload
    # All pins and bytes are checked before the first output mutation.
    output.mkdir(parents=True, exist_ok=False)
    try:
        source = output / "source"
        source.mkdir()
        with tarfile.open(fileobj=__import__("io").BytesIO(archive), mode="r:") as tf:
            for member in tf.getmembers():
                name = safe_relative(member.name)
                target = source.joinpath(*PurePosixPath(name).parts)
                if member.isdir():
                    target.mkdir(parents=True, exist_ok=True)
                elif member.isfile():
                    target.parent.mkdir(parents=True, exist_ok=True)
                    stream = tf.extractfile(member)
                    if stream is None:
                        raise ValueError(f"cannot read archived file: {name}")
                    target.write_bytes(stream.read())
                    target.chmod(member.mode & 0o777)
                else:
                    raise ValueError(f"archive contains unsupported entry: {name}")
        applied = {}
        patches = []
        for path, payload in payloads.items():
            target = source.joinpath(*PurePosixPath(path).parts)
            before = normalized[path]["before_sha256"]
            prior = target.read_bytes() if target.exists() else None
            if (None if prior is None else sha(prior)) != before:
                raise ValueError(f"private extraction preimage mismatch: {path}")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(payload)
            after = target.read_bytes()
            if sha(after) != normalized[path]["after_sha256"]:
                raise ValueError(f"private overlay readback mismatch: {path}")
            applied[path] = {"before_sha256": before, "after_sha256": sha(after),
                             "reversible": (None if prior is None else sha(prior)) == before}
            old_text = (prior or b"").decode("utf-8", errors="surrogateescape").splitlines(True)
            new_text = after.decode("utf-8", errors="surrogateescape").splitlines(True)
            patches.extend(difflib.unified_diff(old_text, new_text,
                          fromfile=f"baseline/{path}", tofile=f"overlay/{path}"))
            if prior is not None and sha(prior) != before:
                raise ValueError(f"reversibility check failed: {path}")
        inv = inventory({p: (source / p).read_bytes() for p in base_files
                         if p.startswith(("src/", "include/", "tests/", "cmake/")) or p == "CMakeLists.txt"})
        for path in payloads:
            if path.startswith(("src/", "include/", "tests/", "cmake/")) or path == "CMakeLists.txt":
                inv[path] = sha((source / path).read_bytes())
        inv = dict(sorted(inv.items()))
        (output / "overlay-payloads").mkdir()
        for idx, (path, payload) in enumerate(payloads.items()):
            (output / "overlay-payloads" / f"{idx:02d}-{Path(path).name}").write_bytes(payload)
        (output / "overlay.patch").write_text("".join(patches), encoding="utf-8", errors="surrogateescape")
        (output / "overlay-input-manifest.json").write_bytes(manifest_path.read_bytes())
        (output / "source-inventory.json").write_text(json.dumps({
            "baseline_source_commit": commit, "files_sha256": inv,
            "inventory_sha256": sha(json.dumps(inv, sort_keys=True, separators=(",", ":")).encode()),
            "scope": "tracked src/include/tests/cmake plus root CMakeLists.txt, after overlay"
        }, indent=2) + "\n")
        result = {"schema": 1, "status": "private_source_prepared",
                  "baseline_source_commit": commit, "launcher_source_commit": head,
                  "source_directory": str(source), "archive_sha256": sha(archive),
                  "input_manifest_sha256": sha(manifest_path.read_bytes()),
                  "overlay_manifest": normalized, "applied": applied,
                  "source_inventory": "source-inventory.json",
                  "source_inventory_sha256": sha((output / "source-inventory.json").read_bytes()),
                  "limitations": ["diagnostic preparation only; not qualification",
                                  "new private source files are not present in baseline Git archive"]}
        (output / "prepared-source.json").write_text(json.dumps(result, indent=2) + "\n")
        return result
    except BaseException:
        # The destination was required to be fresh; remove only our own partial output.
        import shutil
        shutil.rmtree(output)
        raise

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", required=True, type=Path, help="baseline Git checkout")
    p.add_argument("--overlay-dir", required=True, type=Path, help="reviewed overlay directory containing manifest.json")
    p.add_argument("--output", required=True, type=Path, help="new private preparation directory")
    args = p.parse_args()
    print(json.dumps(prepare(args.root, args.overlay_dir, args.output), indent=2))

if __name__ == "__main__":
    main()

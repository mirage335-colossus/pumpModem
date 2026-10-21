#!/usr/bin/env python3
"""Retain a built Symbol Burst diagnostic executable and its runtime provenance."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

# Keep classification aligned with the established GNU runtime exclusion in
# tools/build-sdk.py:378, including libmvec and libBrokenLocale.
GLIBC_NAMES = re.compile(
    r"^(?:ld-linux[^/]*|ld64[^/]*|lib(?:c|m|mvec|dl|pthread|rt|util|resolv|anl|thread_db|BrokenLocale|nss_[^.]+)\.so(?:\..*)?)$"
)


def run(command: list[str], *, timeout: int = 30) -> str:
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=timeout, check=False)
    if completed.returncode:
        raise RuntimeError(f"command failed ({completed.returncode}): {command!r}\n{completed.stdout[-4000:]}")
    return completed.stdout


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_text(path: Path, value: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(value, encoding="utf-8")


def parse_ldd(output: str) -> tuple[list[tuple[str, Path]], list[str]]:
    resolved: list[tuple[str, Path]] = []
    host_abi: list[str] = []
    for raw in output.splitlines():
        line = raw.strip()
        if not line or line.startswith("linux-vdso"):
            continue
        if "not found" in line:
            raise RuntimeError(f"unresolved runtime dependency: {line}")
        match = re.match(r"([^\s]+)\s+=>\s+(/[^\s]+)", line)
        if match:
            name, resolved_path = match.groups()
        else:
            match = re.match(r"(/[^\s]+)\s+\(0x[0-9a-fA-F]+\)", line)
            if not match:
                raise RuntimeError(f"unrecognized ldd dependency line: {line}")
            resolved_path = match.group(1)
            name = Path(resolved_path).name
        if GLIBC_NAMES.match(name) or GLIBC_NAMES.match(Path(resolved_path).name):
            host_abi.append(name)
        else:
            path = Path(resolved_path)
            if not path.is_file():
                raise RuntimeError(f"resolved dependency is not a file: {path}")
            resolved.append((name, path))
    return resolved, sorted(set(host_abi))


def package_metadata(path: Path, docs: Path) -> dict[str, object]:
    owners_output = run(["dpkg-query", "-S", str(path.resolve())])
    owners = []
    for line in owners_output.splitlines():
        package = line.split(": ", 1)[0]
        if package and package not in owners:
            owners.append(package)
    records = []
    for package in owners:
        version_output = run(["dpkg-query", "-W", "-f=${Version}", package]).strip()
        package_names = [package]
        unqualified = package.split(":", 1)[0]
        if unqualified not in package_names:
            package_names.append(unqualified)
        candidates = [Path("/usr/share/doc") / name / "copyright" for name in package_names]
        copyright_path = next((candidate for candidate in candidates if candidate.is_file()), None)
        copied_copyright = None
        if copyright_path is not None:
            destination = docs / package.replace("/", "_").replace(":", "_") / "copyright"
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(copyright_path, destination)
            copied_copyright = str(destination.relative_to(docs.parent))
        records.append({"package": package, "version": version_output,
                        "copyright_status": "copied" if copied_copyright else "not_available",
                        "copyright_candidates_checked": [str(path) for path in candidates],
                        "copyright_file": copied_copyright})
    return {"owners": owners, "packages": records}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--runtime-probe", required=True, choices=("on", "off"))
    args = parser.parse_args()
    root = args.root.resolve()
    report = args.report.resolve()
    binary = root / "build/test_symbol_burst_live"
    parent = root / "build/symbol-burst-live-qualification"
    source = root / "tests/test_symbol_burst_live.cpp"
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise RuntimeError(f"built diagnostic executable is missing or not executable: {binary}")
    if not source.is_file():
        raise RuntimeError(f"test source is missing: {source}")
    if not report.is_relative_to(parent.resolve()):
        raise RuntimeError(f"retained output must be inside the qualification reproducer directory: {report}")
    if report.exists() and any(report.iterdir()):
        raise RuntimeError(f"refusing to overwrite existing retained artifact: {report}")
    report.mkdir(parents=True, exist_ok=True)
    runtime_dir = report / "runtime"
    runtime_dir.mkdir()

    # Preserve the exact CI-built object. Never rebuild or execute the exported copy.
    retained_binary = report / binary.name
    shutil.copyfile(binary, retained_binary)
    shutil.copystat(binary, retained_binary)
    if sha256(binary) != sha256(retained_binary):
        raise RuntimeError("retained ELF hash differs from the CI-built executable")

    retained_inputs = {}
    for name in ("source.txt", "source-tree.txt", "CMakeCache.txt", "source-inventory.json"):
        origin = parent / name
        if not origin.is_file():
            raise RuntimeError(f"required qualification provenance is missing: {origin}")
        destination = report / name
        shutil.copyfile(origin, destination)
        retained_inputs[name] = {"source": str(origin), "sha256": sha256(destination)}
    source_copy = report / "test_symbol_burst_live.cpp"
    shutil.copyfile(source, source_copy)
    retained_inputs[source_copy.name] = {"source": str(source), "sha256": sha256(source_copy)}
    notice_sources = {
        "LICENSE": root / "LICENSE",
        "QR-LICENSE": root / "third_party/qrcodegen/LICENSE",
        "XZ-LICENSE": root / "third_party/xz/COPYING.0BSD",
        "XZ-SOURCE.md": root / "third_party/xz/README.datapump.md",
    }
    retained_notices = []
    for installed_name, origin in notice_sources.items():
        if not origin.is_file():
            raise RuntimeError(f"installed runtime notice source is missing: {origin}")
        destination = report / "notices" / installed_name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(origin, destination)
        retained_notices.append({"source": str(origin), "retained_path": str(destination.relative_to(report)),
                                 "sha256": sha256(destination)})

    ldd_text = run(["ldd", str(binary)])
    write_text(report / "ldd.txt", ldd_text)
    dependencies, host_abi = parse_ldd(ldd_text)
    runtime_records = []
    seen_destinations: dict[str, str] = {}
    copyright_root = report / "copyright"
    for soname, original_path in dependencies:
        canonical = original_path.resolve(strict=True)
        filename = Path(soname).name
        destination = runtime_dir / filename
        digest = sha256(canonical)
        previous = seen_destinations.get(filename)
        if previous and previous != digest:
            raise RuntimeError(f"two dependency objects collide at retained path {filename}")
        if not destination.exists():
            shutil.copyfile(canonical, destination)
        seen_destinations[filename] = digest
        metadata = package_metadata(canonical, copyright_root)
        runtime_records.append({"name": soname, "resolved_path": str(original_path),
                                "canonical_path": str(canonical), "retained_path": str(destination.relative_to(report)),
                                "sha256": digest, **metadata})

    cache = (root / "build/CMakeCache.txt").read_text(errors="replace")
    compiler_match = re.search(r"^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING)=(.+)$", cache, re.MULTILINE)
    compiler = compiler_match.group(1) if compiler_match else None
    if not compiler or not Path(compiler).is_file():
        raise RuntimeError("CMake cache does not identify an available C++ compiler")
    compiler_version = run([compiler, "--version"])
    write_text(report / "compiler-version.txt", compiler_version)
    elf_report = run(["readelf", "-h", "-d", str(binary)])
    write_text(report / "readelf-elf-dynamic.txt", elf_report)
    compiler_id = run(["ninja", "--version"]).strip()
    write_text(report / "ninja-version.txt", compiler_id + "\n")
    command_lines = run(["ninja", "-C", str(root / "build"), "-t", "commands", "test_symbol_burst_live"], timeout=60)
    if len(command_lines.encode("utf-8")) > 5 * 1024 * 1024:
        raise RuntimeError("target compile/link command evidence exceeds the 5 MiB retention bound")
    write_text(report / "target-commands.txt", command_lines)
    compile_db = root / "build/compile_commands.json"
    if compile_db.is_file():
        entries = json.loads(compile_db.read_text())
        selected = [entry for entry in entries if Path(entry.get("file", "")).name == source.name]
        if not selected:
            raise RuntimeError("compile_commands.json has no command for the retained test source")
        write_text(report / "test-compile-command.json", json.dumps(selected, indent=2) + "\n")

    host_version = run(["ldd", "--version"]).splitlines()[0]
    files = {}
    for path in sorted(p for p in report.rglob("*") if p.is_file()):
        files[str(path.relative_to(report))] = {"sha256": sha256(path), "size_bytes": path.stat().st_size}
    manifest = {
        "schema": "paced-symbol-burst-original-context-reproducer-v1",
        "classification": "diagnostic artifact; not qualification or release content",
        "source_commit": (parent / "source.txt").read_text().strip(),
        "source_tree": (parent / "source-tree.txt").read_text().strip(),
        "test_target": "test_symbol_burst_live",
        "test_source": str(source),
        "test_source_sha256": sha256(source),
        "built_elf_source": str(binary),
        "built_elf_sha256": sha256(binary),
        "retained_elf": retained_binary.name,
        "retained_elf_sha256": sha256(retained_binary),
        "runtime_probe_arm": args.runtime_probe,
        "runtime_dependencies": runtime_records,
        "unresolved_dependencies": [],
        "host_provided_glibc_abi_dependencies": host_abi,
        "host_glibc_runtime": host_version,
        "glibc_loader_and_runtime_not_redistributed": True,
        "compiler": compiler,
        "compiler_version_file": "compiler-version.txt",
        "ninja_version_file": "ninja-version.txt",
        "target_compile_and_link_commands": "target-commands.txt",
        "elf_dynamic_inventory": "readelf-elf-dynamic.txt",
        "ldd_inventory": "ldd.txt",
        "retained_inputs": retained_inputs,
        "installed_runtime_notices": retained_notices,
        "files": files,
    }
    write_text(report / "manifest.json", json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"artifact": str(report), "elf_sha256": sha256(retained_binary),
                      "runtime_dependency_count": len(runtime_records),
                      "host_provided_glibc_abi_dependencies": host_abi}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        print(f"retain-paced-diagnostic: {error}", file=sys.stderr)
        raise SystemExit(1)

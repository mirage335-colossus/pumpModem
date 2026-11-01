#!/usr/bin/env python3
"""Create a deterministic, redaction-checked technical evidence archive.

The default is a dry-run. Pass --build only after the study owner confirms the
evidence graph is complete. Archive members are selected explicitly and sorted;
tar ownership, timestamps, modes, and gzip timestamp are normalized.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import tarfile
import gzip


PREFIX = "technical-evidence"
EXCLUDED_SUFFIXES = {".tar.gz", ".nsys-rep"}
RAW_SUFFIXES = {".c128", ".u8"}
RAW_INPUT_CASES = {"boundary-cn-10-r0", "large-bank"}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def ignored(path: Path) -> bool:
    return "__pycache__" in path.parts or path.name.endswith(tuple(EXCLUDED_SUFFIXES))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence-root", type=Path, required=True,
                        help="local-evidence/triad-fp4-20261101")
    parser.add_argument("--source-root", type=Path, required=True,
                        help="repository root containing current sources and build/dev/triad_long")
    parser.add_argument("--output", type=Path, required=True,
                        help="destination technical-evidence.tar.gz")
    parser.add_argument("--build", action="store_true",
                        help="write archive (default only prints the selected file plan)")
    args = parser.parse_args()
    evidence = args.evidence_root.resolve()
    source = args.source_root.resolve()
    output = args.output.resolve()
    if not evidence.is_dir() or not source.is_dir():
        parser.error("evidence-root and source-root must be existing directories")
    if args.build and output.exists():
        parser.error(f"refusing to overwrite existing archive: {output}")

    members: dict[str, Path] = {}

    def add(path: Path, member: str) -> None:
        if ignored(path):
            return
        if not path.is_file() or path.is_symlink():
            return
        if path.suffix == ".sqlite":
            try:
                rel = path.relative_to(evidence).as_posix()
            except ValueError as exc:
                raise ValueError(f"SQLite path is outside evidence root: {path}") from exc
            expected = approved_sqlite.get(rel)
            if expected is None or sha256(path) != expected:
                raise ValueError(f"SQLite missing exact sanitized hash approval: {rel}")
            sqlite_seen.add(rel)
        member = PurePosixPath(PREFIX, member).as_posix()
        old = members.get(member)
        if old is not None and old.resolve() != path.resolve():
            raise ValueError(f"archive path collision: {member}")
        members[member] = path

    def add_tree(base: Path, archive_base: str, *, allow=lambda p: True) -> None:
        if not base.exists():
            raise FileNotFoundError(base)
        for path in sorted(base.rglob("*")):
            if path.is_file() and not path.is_symlink() and not ignored(path) and allow(path):
                add(path, (PurePosixPath(archive_base) / path.relative_to(base)).as_posix())

    # Device results include all final logs, JSON records and other compact
    # artifacts. Raw Nsight reports stay private. SQLite is admitted only when
    # its path and sanitized digest are explicitly present in the redaction map.
    redaction_path = evidence / "profile-redaction.json"
    redaction = json.loads(redaction_path.read_text())
    approved_sqlite = {row["path"]: row["sanitized_sha256"] for row in redaction
                       if row.get("event_tables_unchanged") is True}
    sqlite_seen: set[str] = set()
    for device, folder in (("gpu-5090", "5090-final/results"),
                           ("gpu-pro", "pro-final/results"),
                           ("gpu-mig", "mig-final/results")):
        base = evidence / folder
        for path in sorted(base.rglob("*")):
            if not path.is_file() or path.is_symlink() or ignored(path):
                continue
            if path.suffix == ".sqlite":
                rel = path.relative_to(evidence).as_posix()
                expected = approved_sqlite.get(rel)
                if expected is None or sha256(path) != expected:
                    raise ValueError(f"SQLite missing exact sanitized hash approval: {rel}")
                sqlite_seen.add(rel)
            add(path, (PurePosixPath(device, "results") / path.relative_to(base)).as_posix())

    if not sqlite_seen:
        raise ValueError("no approved sanitized SQLite profiler traces found")

    # Preserve all sampled-input metadata and logs, but include raw waveform
    # payloads only for the named boundary and representative large-bank cases.
    input_root = evidence / "inputs"
    for path in sorted(input_root.rglob("*")):
        if not path.is_file() or path.is_symlink() or ignored(path):
            continue
        rel = path.relative_to(input_root)
        if path.suffix in RAW_SUFFIXES and (rel.parts[0] if rel.parts else "") not in RAW_INPUT_CASES:
            continue
        if path.suffix not in RAW_SUFFIXES and path.suffix not in {".json", ".txt"}:
            continue
        add(path, (PurePosixPath("development-inputs/inputs") / rel).as_posix())

    # Holdout sampled IQ is regenerated from the frozen roots and exporter; keep
    # the complete manifests and provenance but do not duplicate raw payloads.
    holdout_inputs = evidence / "holdout-evidence/holdout-inputs"
    add_tree(holdout_inputs, "holdout-evidence/holdout-inputs",
             allow=lambda p: p.suffix in {".json", ".txt"})
    for rel in ("holdout-evidence/generate-holdout.py",
                "holdout-evidence/run-campaign.py",
                "holdout-summary.json"):
        add(evidence / rel, rel)
    add_tree(evidence / "holdout-evidence/tools", "holdout-evidence/tools",
             allow=lambda p: p.suffix in {".py", ".cpp", ".hpp", ".json", ".txt"})
    add_tree(evidence / "holdout-evidence/results", "holdout-evidence/results")

    # Frozen development exporter and the current, hash-bound exporter build.
    add_tree(evidence / "source-before-seed-extension", "source-before-seed-extension")
    for rel in ("build/dev/triad_long", "tools/triad-long.cpp",
                "include/datapump/triad_modem_v3.hpp", "src/triad_simulation_stream.cpp"):
        path = source / rel
        if not path.is_file():
            raise FileNotFoundError(path)
        add(path, (PurePosixPath("exporter-current") / rel).as_posix())

    # Current study-specific Python source and the prior correlation backend.
    add_tree(source / "tools", "source-final/tools",
             allow=lambda p: p.name.startswith("triad") and p.suffix == ".py")
    test_names = ("test_triad_exhaustive_correlation.py", "test_triad_fp4_campaign.py",
                  "test_triad_fp4_native.py", "test_triad_fp4_representation.py",
                  "test_triad_scan_workspace.py")
    for name in test_names:
        add(source / "tests" / name, f"source-final/tests/{name}")
    add_tree(evidence / "source-baseline", "source-baseline")

    # Profiling, kernel investigation, environment and failed qualification
    # evidence are included as compact technical records.
    add_tree(evidence / "profile-analysis", "profile-analysis")
    add_tree(evidence / "static-kernel-analysis", "static-kernel-analysis")
    for rel in ("profile-redaction.json", "triad-focused-ctest.log",
                "python-correlation-tests.log", "python-fp4-tests.log",
                "python-workspace-tests.log",
                "whole-scan-qualification-first-failure.log",
                "whole-scan-qualification-second-failure.log",
                "whole-scan-qualification-third-failure.log",
                "hardware-fp4-environment.json", "hardware-pro-environment.json",
                "hardware-mig-environment.json"):
        add(evidence / rel, rel)

    ordered = sorted(members.items())
    if not ordered:
        raise ValueError("empty archive selection")
    if args.build:
        output.parent.mkdir(parents=True, exist_ok=True)

    checksums = "".join(f"{sha256(path)}  {name}\n" for name, path in ordered).encode()
    planned = ordered + [(f"{PREFIX}/SHA256SUMS", None)]
    total_bytes = sum(path.stat().st_size for _, path in ordered) + len(checksums)
    print(json.dumps({"mode": "build" if args.build else "dry-run",
                      "output": str(output), "file_count_including_sha256sums": len(planned),
                      "uncompressed_bytes": total_bytes,
                      "sqlite_verified": sorted(sqlite_seen),
                      "members": [name for name, _ in planned]}, indent=2))
    if not args.build:
        return 0

    with output.open("xb") as raw:
        with gzip.GzipFile(fileobj=raw, mode="wb", filename="", mtime=0, compresslevel=9) as gz:
            with tarfile.open(fileobj=gz, mode="w", format=tarfile.USTAR_FORMAT) as tar:
                for name, path in ordered:
                    info = tarfile.TarInfo(name)
                    info.size = path.stat().st_size
                    info.mtime = 0
                    info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    info.mode = 0o755 if os.access(path, os.X_OK) else 0o644
                    with path.open("rb") as f:
                        tar.addfile(info, f)
                info = tarfile.TarInfo(f"{PREFIX}/SHA256SUMS")
                info.size = len(checksums)
                info.mtime = 0
                info.uid = info.gid = 0
                info.uname = info.gname = ""
                info.mode = 0o644
                tar.addfile(info, io.BytesIO(checksums))
    print(f"wrote {output} ({output.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Collect a source-bound GCC gcov line/branch summary for native triad v3.

This reports compiler-instrumented counters. gcov branch records are not
semantic condition, decision, or receiver-behavior coverage.
"""

from __future__ import annotations

import argparse
import hashlib
import gzip
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time


SCHEMA_VERSION = 2
DEFAULT_SOURCES = (
    "src/triad_statistics.cpp",
    "src/triad_modem_v3.cpp",
    "src/triad_simulation_stream.cpp",
    "src/triad_receiver_v3.cpp",
)
HEADER_COVERAGE_TRANSLATION_UNITS = (
    "tests/test_triad_v3_source.cpp",
    "tests/test_triad_v3_channel.cpp",
    "tests/test_triad_v3_receiver.cpp",
    "tests/test_triad_statistics.cpp",
)
BRANCH_LIMITATION = (
    "GCC gcov branch records are compiler CFG edges, may include compiler- or "
    "exception-related edges, and do not establish semantic condition/decision "
    "or behavioral coverage. Negative counters are retained as invalid and make "
    "the affected branch scope unqualified. No branch exclusions are applied."
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_gcov_json(path: Path, expected_source: Path) -> dict:
    try:
        with gzip.open(path, "rt", encoding="utf-8") as stream:
            document = json.load(stream)
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read gcov JSON {path}: {error}") from error
    expected_path = expected_source.resolve(strict=True)
    gcov_cwd = Path(document.get("current_working_directory") or path.parent)
    if not gcov_cwd.is_absolute():
        gcov_cwd = path.parent / gcov_cwd
    matches = []
    for item in document.get("files", []):
        candidate = Path(item.get("file", ""))
        if not candidate.is_absolute():
            candidate = gcov_cwd / candidate
        if candidate.resolve() == expected_path:
            matches.append(item)
    if len(matches) != 1:
        raise ValueError(f"{path}: expected exactly one gcov file resolving to {expected_path}, got {len(matches)}")

    line_hits = {}
    invalid_line_counts = {}
    branch_total = branch_hit = branch_miss = branch_invalid = 0
    for line in matches[0].get("lines", []):
        count = line.get("count")
        number = line.get("line_number")
        if not isinstance(number, int) or not isinstance(count, int):
            continue
        if count < 0:
            invalid_line_counts.setdefault(number, []).append(count)
        else:
            line_hits[number] = max(count, line_hits.get(number, 0))
        for branch in line.get("branches", []):
            branch_count = branch.get("count")
            if isinstance(branch_count, int):
                branch_total += 1
                branch_hit += branch_count > 0
                branch_miss += branch_count == 0
                branch_invalid += branch_count < 0
    executable = {line: count for line, count in line_hits.items() if count >= 0}
    executed = {line: count for line, count in executable.items() if count > 0}
    return {
        "lines": {
            "instrumented": len(executable),
            "executed": len(executed),
            "uncovered_source_lines": sorted(line for line, count in executable.items() if count == 0),
            "invalid_negative_counts": {str(line): counts for line, counts in invalid_line_counts.items()},
            "coverage_status": "unqualified_negative_line_counters" if invalid_line_counts else "counted",
        },
        "gcov_branches": {
            "instrumented_outcomes": branch_total,
            "taken_outcomes": branch_hit,
            "untaken_outcomes": branch_miss,
            "invalid_negative_outcomes": branch_invalid,
            "coverage_status": "unqualified_negative_branch_counters" if branch_invalid else "counted",
        },
    }


def header_inventory(source_root: Path) -> dict[str, str]:
    """Hash project C/C++ headers in the two maintained code roots."""
    result = {}
    for folder in (source_root / "include", source_root / "src"):
        for path in sorted(folder.rglob("*")):
            if path.is_file() and path.suffix in {".h", ".hh", ".hpp", ".hxx"}:
                result[path.relative_to(source_root).as_posix()] = sha256(path)
    return result


def coverage_source_list(source_root: Path) -> tuple[str, ...]:
    """Use the maintained native report scope when available."""
    matrix = source_root / "tools" / "triad-performance-coverage.json"
    if not matrix.is_file():
        return DEFAULT_SOURCES
    try:
        report = json.loads(matrix.read_text(encoding="utf-8"))
        sources = report["coverage_reports"]["native"]["source_files"]
    except (OSError, json.JSONDecodeError, KeyError, TypeError) as error:
        raise ValueError(f"cannot read native source scope from {matrix}: {error}") from error
    if not isinstance(sources, list) or not sources or any(not isinstance(item, str) for item in sources):
        raise ValueError(f"invalid native source_files in {matrix}")
    return tuple(dict.fromkeys(sources))


def dependency_inventory(source_root: Path, native_sources: tuple[str, ...]) -> dict[str, str]:
    """Hash declared native source/test/header/build inputs using the wrapper's row rule.

    This is a declared applicability inventory. It is not a compiler-emitted
    dependency closure; compile_commands and object timestamps are recorded
    separately and do not prove which header bytes were compiled.
    """
    matrix_path = source_root / "tools" / "triad-performance-coverage.json"
    names = set(native_sources)
    if matrix_path.is_file():
        try:
            matrix = json.loads(matrix_path.read_text(encoding="utf-8"))
            rows = matrix.get("rows", [])
        except (OSError, json.JSONDecodeError) as error:
            raise ValueError(f"cannot read dependency rows from {matrix_path}: {error}") from error
        scope = set(native_sources)
        for row in rows:
            row_sources = set(row.get("source_files", []))
            if not row_sources.intersection(scope):
                continue
            names.update(row_sources)
            for test in row.get("cheap_functional_tests", []):
                if isinstance(test, str):
                    path = test.split("::", 1)[0]
                    if path.startswith(("tests/", "tools/")):
                        names.add(path)
    names.add("CMakeLists.txt")
    names.update(path.relative_to(source_root).as_posix()
                 for path in (source_root / "cmake").rglob("*.cmake"))
    # Preserve exact included project-header identities even when a header is
    # not listed as a row-level source dependency.
    names.update(header_inventory(source_root))
    result = {}
    for name in sorted(names):
        path = (source_root / name).resolve(strict=True)
        if not path.is_relative_to(source_root):
            raise ValueError(f"declared dependency is outside source root: {name}")
        result[name] = sha256(path)
    return result


def require_inventory_match(expected: dict[str, str], actual: dict[str, str], label: str) -> None:
    changed = sorted(name for name in set(expected) | set(actual)
                     if expected.get(name) != actual.get(name))
    if changed:
        raise ValueError(f"{label} changed between pre-build guard and capture: " + ", ".join(changed[:12]))


def aggregate_header_documents(source_root: Path, records: list[tuple[str, dict]]) -> dict:
    """Merge header counters across selected translation units, retaining TU scope."""
    merged: dict[str, dict] = {}
    for record_name, document in records:
        for item in document.get("files", []):
            try:
                path = Path(item.get("file", ""))
                if not path.is_absolute():
                    cwd = Path(document.get("current_working_directory") or source_root)
                    if not cwd.is_absolute():
                        cwd = source_root / cwd
                    path = cwd / path
                path = path.resolve(strict=True)
                relative = path.relative_to(source_root).as_posix()
            except (OSError, ValueError):
                continue
            if not relative.startswith(("include/", "src/")) or path.suffix not in {".h", ".hh", ".hpp", ".hxx"}:
                continue
            target = merged.setdefault(relative, {"lines": {}, "branches": {}, "gcov_records": set()})
            target["gcov_records"].add(record_name)
            for line in item.get("lines", []):
                number, count = line.get("line_number"), line.get("count")
                function = line.get("function_name")
                if not isinstance(number, int) or not isinstance(count, int):
                    continue
                line_key = (number, function or "")
                if count < 0:
                    target.setdefault("invalid_line_counts", {}).setdefault(line_key, []).append(count)
                else:
                    target["lines"][line_key] = max(count, target["lines"].get(line_key, 0))
                for ordinal, branch in enumerate(line.get("branches", [])):
                    branch_count = branch.get("count")
                    if not isinstance(branch_count, int):
                        continue
                    # gcov source/destination block IDs are local to a compiled TU.
                    # Keep the record identity in the key; no CFG equivalence is
                    # inferred across translation units.
                    key = (record_name, line_key, branch.get("source_block_id"),
                           branch.get("destination_block_id"), branch.get("fallthrough"),
                           branch.get("throw"), ordinal)
                    target["branches"][key] = max(branch_count, target["branches"].get(key, branch_count))

    inventory = header_inventory(source_root)
    output = {}
    for relative, values in sorted(merged.items()):
        line_counts = values["lines"]
        branch_counts = values["branches"].values()
        unique_lines = {}
        for (number, _function), count in line_counts.items():
            unique_lines[number] = max(count, unique_lines.get(number, 0))
        invalid_branch_count = sum(count < 0 for count in values["branches"].values())
        output[relative] = {
            "sha256_at_capture": inventory[relative],
            "lines": {
                "instrumented": len(unique_lines),
                "executed": sum(count > 0 for count in unique_lines.values()),
                "uncovered_source_lines": sorted(number for number, count in unique_lines.items() if count == 0),
                "invalid_negative_counts": {
                    str(number): counts for (number, _function), counts in values.get("invalid_line_counts", {}).items()
                },
                "coverage_status": "unqualified_negative_line_counters"
                if values.get("invalid_line_counts") else "counted",
            },
            "gcov_branches": {
                "instrumented_outcomes": len(values["branches"]),
                "taken_outcomes": sum(count > 0 for count in branch_counts),
                "untaken_outcomes": sum(count == 0 for count in values["branches"].values()),
                "invalid_negative_outcomes": invalid_branch_count,
                "coverage_status": "unqualified_negative_branch_counters" if invalid_branch_count else "counted",
                "unit": "translation-unit-qualified gcov CFG edge outcomes; no cross-TU edge equivalence asserted",
            },
            "gcov_records": sorted(values["gcov_records"]),
        }
    return output


def compiler_identity(build: Path) -> dict:
    cache_path = build / "CMakeCache.txt"
    if not cache_path.is_file():
        raise ValueError(f"missing CMake cache: {cache_path}")
    cache = {}
    for row in cache_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if ":" in row and not row.startswith("//") and not row.startswith("#"):
            key, value = row.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    compiler = cache.get("CMAKE_CXX_COMPILER")
    if not compiler:
        raise ValueError("CMAKE_CXX_COMPILER missing from CMakeCache.txt")
    version = subprocess.run([compiler, "--version"], check=True, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout.splitlines()[0]
    cmake_compilers = sorted((build / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
    compiler_config = cmake_compilers[-1].read_text(encoding="utf-8", errors="replace") if cmake_compilers else ""

    def cmake_value(name):
        found = re.search(rf'^set\({name} "([^"]*)"\)', compiler_config, re.MULTILINE)
        return found.group(1) if found else None

    return {
        "path": compiler,
        "version": version,
        "cmake_id": cmake_value("CMAKE_CXX_COMPILER_ID"),
        "cmake_version": cmake_value("CMAKE_CXX_COMPILER_VERSION"),
        "build_type": cache.get("CMAKE_BUILD_TYPE"),
        "coverage_flags": {
            "cxx": cache.get("CMAKE_CXX_FLAGS", ""),
            "exe_link": cache.get("CMAKE_EXE_LINKER_FLAGS", ""),
        },
        "cmake_cache_sha256": sha256(cache_path),
        "compile_commands_sha256": sha256(build / "compile_commands.json"),
    }


def run_gcov(source_root: Path, build: Path, raw_dir: Path, gcov: str,
             source_files: tuple[str, ...]) -> dict[str, Path]:
    raw_dir.mkdir(parents=True, exist_ok=True)
    outputs = {}
    for relative in (*source_files, *HEADER_COVERAGE_TRANSLATION_UNITS):
        source = source_root / relative
        if not source.is_file():
            raise ValueError(f"missing source: {source}")
        objects = list(build.rglob(source.name + ".gcno"))
        if len(objects) != 1:
            raise ValueError(f"expected one .gcno for {relative} in {build}, found {len(objects)}")
        gcov_prefix = objects[0].with_suffix(".o")
        raw_output = raw_dir / f"{source.stem}.gcov.json.gz"
        raw_output.unlink(missing_ok=True)
        command = [gcov, "--json-format", "-b", "-c", "-o", str(gcov_prefix), str(source)]
        result = subprocess.run(command, cwd=raw_dir, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        if result.returncode:
            raise ValueError(f"gcov failed for {relative}: {result.stdout.strip()}")
        if not raw_output.is_file():
            raise ValueError(f"gcov did not produce one retained JSON file for {relative}: {result.stdout.strip()}")
        outputs[relative] = raw_output
    return outputs


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--raw-dir", type=Path,
                        help="directory retaining the original compressed gcov JSON")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--gcov", default="gcov", help="GCC gcov executable")
    parser.add_argument("--executed-check", action="append", default=[],
                        help="record a focused check already run (repeatable; does not execute it)")
    parser.add_argument("--header-guard-check", type=Path,
                        help="require declared dependency/header hashes to match a pre-build guard manifest")
    parser.add_argument("--write-header-guard", type=Path,
                        help="write a pre-build SHA-256 manifest for declared dependencies and headers, then exit")
    args = parser.parse_args(argv)
    try:
        source_root = args.source_root.resolve(strict=True)
        if args.write_header_guard:
            guard_path = args.write_header_guard.resolve()
            guard_path.parent.mkdir(parents=True, exist_ok=True)
            source_files = coverage_source_list(source_root)
            guard = {
                "schema": "datapump.triad.native-gcov-header-guard",
                "schema_version": 2,
                "source_root": str(source_root),
                "created_at_unix_ns": time.time_ns(),
                "native_source_files": list(source_files),
                "coverage_matrix_sha256": sha256(source_root / "tools" / "triad-performance-coverage.json"),
                "headers": header_inventory(source_root),
                "dependencies": dependency_inventory(source_root, source_files),
            }
            guard_path.write_text(json.dumps(guard, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            print(guard_path)
            return 0
        if args.build_dir is None or args.raw_dir is None or args.output is None:
            raise ValueError("collection requires --build-dir, --raw-dir, and --output")
        build = args.build_dir.resolve(strict=True)
        raw_dir = args.raw_dir.resolve()
        guard_hash = None
        guard_matches = None
        dependency_guard_matches = None
        guard_created_at_ns = None
        guard = None
        if args.header_guard_check:
            guard_path = args.header_guard_check.resolve(strict=True)
            guard = json.loads(guard_path.read_text(encoding="utf-8"))
            expected_headers = guard.get("headers")
            current_headers = header_inventory(source_root)
            if not isinstance(expected_headers, dict):
                raise ValueError("header guard has no headers object")
            require_inventory_match(expected_headers, current_headers, "headers")
            source_files = coverage_source_list(source_root)
            if guard.get("native_source_files") != list(source_files):
                raise ValueError("native source scope changed between pre-build guard and capture")
            matrix_hash_at_capture = sha256(source_root / "tools" / "triad-performance-coverage.json")
            if guard.get("coverage_matrix_sha256") != matrix_hash_at_capture:
                raise ValueError("coverage matrix changed between pre-build guard and capture")
            expected_dependencies = guard.get("dependencies")
            current_dependencies = dependency_inventory(source_root, source_files)
            if not isinstance(expected_dependencies, dict):
                raise ValueError("dependency guard has no dependencies object")
            require_inventory_match(expected_dependencies, current_dependencies, "declared dependencies")
            guard_hash = sha256(guard_path)
            guard_matches = True
            dependency_guard_matches = True
            guard_created_at_ns = guard.get("created_at_unix_ns")
        else:
            source_files = coverage_source_list(source_root)
        compiler = compiler_identity(build)
        gcov_outputs = run_gcov(source_root, build, raw_dir, args.gcov, source_files)
        sources = {}
        gcov_records = []
        translation_units = {}
        for relative, raw in gcov_outputs.items():
            with gzip.open(raw, "rt", encoding="utf-8") as stream:
                document = json.load(stream)
            # Validate exact source identity for every counter-contributing TU,
            # including the supplemental test translation units.
            parse_gcov_json(raw, source_root / relative)
            gcov_records.append((relative, document))
            note_files = list(build.rglob(Path(relative).name + ".gcno"))
            if len(note_files) != 1:
                raise ValueError(f"expected one .gcno identity for {relative}, found {len(note_files)}")
            object_path = note_files[0].with_suffix(".o")
            data_path = note_files[0].with_suffix(".gcda")
            object_mtime_ns = object_path.stat().st_mtime_ns
            translation_units[relative] = {
                "sha256": sha256(source_root / relative),
                "gcov_data": str(raw),
                "object_path": str(object_path),
                "gcno_path": str(note_files[0]),
                "gcda_path": str(data_path),
                "object_mtime_ns": object_mtime_ns,
                "gcda_mtime_ns": data_path.stat().st_mtime_ns if data_path.exists() else None,
                "object_mtime_after_prebuild_header_guard":
                    object_mtime_ns >= guard_created_at_ns if isinstance(guard_created_at_ns, int) else None,
            }
        for relative in source_files:
            raw = gcov_outputs[relative]
            source = source_root / relative
            result = parse_gcov_json(raw, source)
            result["sha256"] = sha256(source)
            result["raw_gcov_json_gz"] = os.path.relpath(raw, args.output.parent.resolve())
            sources[relative] = result
        payload = {
            "schema": "datapump.triad.native-gcov-coverage",
            "schema_version": SCHEMA_VERSION,
            "scope": "maintained native coverage report source rows, plus focused test-TU header counters",
            "source_root": str(source_root),
            "source_revision": subprocess.run(["git", "-C", str(source_root), "rev-parse", "HEAD"],
                                               check=True, text=True, stdout=subprocess.PIPE).stdout.strip(),
            "working_tree_dirty": bool(subprocess.run(
                ["git", "-C", str(source_root), "status", "--porcelain"], check=True,
                text=True, stdout=subprocess.PIPE).stdout.strip()),
            "build_dir": str(build),
            "executed_checks": args.executed_check,
            "counter_translation_units": translation_units,
            "compiler": compiler,
            "gcov": subprocess.run([args.gcov, "--version"], check=True, text=True,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout.splitlines()[0],
            "coverage_interpretation": {
                "line_metric": "source lines with gcov execution counters",
                "branch_metric": BRANCH_LIMITATION,
                "declared_exclusions": [],
            },
            "sources": sources,
            "dependency_sha256": dependency_inventory(source_root, source_files),
            "headers": aggregate_header_documents(source_root, gcov_records),
            "header_counter_applicability": {
                "line_merge": "executed source lines are unioned by header-relative path and line; counter values are not summed across TUs",
                "branch_merge": "GCOV edges remain TU-qualified because block IDs are local; no CFG equivalence is inferred across TUs",
                "source_hash_binding": "each header has a capture-time SHA-256; prebuild_postcapture_header_inventory_match means only the before/after inventories matched",
                "build_binding": "per-TU object/note/data paths and mtimes are retained; object_mtime_after_prebuild_header_guard records temporal order, not embedded source/header hashes",
                "prebuild_guard_sha256": guard_hash,
                "coverage_matrix_sha256_at_capture": sha256(source_root / "tools" / "triad-performance-coverage.json"),
                "prebuild_postcapture_header_inventory_match": guard_matches,
                "prebuild_postcapture_declared_dependency_inventory_match": dependency_guard_matches,
                "declared_dependency_scope": "native report source_files; applicable row source_files and test paths; project headers; CMakeLists.txt and cmake/**/*.cmake",
                "dependency_limit": "declared applicability inventory, not compiler-emitted complete dependency closure; compile commands and object mtimes do not bind included header bytes",
                "counter_translation_units_built_after_guard": all(
                    item["object_mtime_after_prebuild_header_guard"] is True
                    for item in translation_units.values()) if guard_matches else None,
                "historical_reports_without_header_guard": "header counters are unqualified to the captured header bytes",
            },
        }
        if guard is not None:
            # Re-hash after gcov parsing/report assembly, not only immediately
            # before it. Otherwise a source/header/test edit during collection
            # could be labeled as matching the earlier build guard.
            require_inventory_match(guard["headers"], header_inventory(source_root), "headers")
            final_dependencies = dependency_inventory(source_root, source_files)
            require_inventory_match(guard["dependencies"], final_dependencies, "declared dependencies")
            if guard["coverage_matrix_sha256"] != sha256(source_root / "tools" / "triad-performance-coverage.json"):
                raise ValueError("coverage matrix changed between pre-build guard and capture")
            for relative, row in payload["sources"].items():
                if row["sha256"] != guard["dependencies"].get(relative):
                    raise ValueError(f"source row {relative} does not match its guarded bytes")
            for relative, row in payload["counter_translation_units"].items():
                if row["sha256"] != guard["dependencies"].get(relative):
                    raise ValueError(f"counter translation unit {relative} does not match its guarded bytes")
            for relative, row in payload["headers"].items():
                if row["sha256_at_capture"] != guard["headers"].get(relative):
                    raise ValueError(f"header counter {relative} does not match its guarded bytes")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"triad_perf_coverage.py: {error}", file=sys.stderr)
        return 2
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

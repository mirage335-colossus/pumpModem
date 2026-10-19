#!/usr/bin/env python3
"""Run focused production test components before the simulation-clock Live gate.

This helper executes binaries built from the checkout in place. It does not
prepare an overlay, mutate application sources, or build the application.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import sys
import time


PATTERN_SELECTORS = (
    "native peak",
    "native point",
    "diagnostic history retention",
    "qualified UTC FFT partial EOF threshold",
)
SOURCE_PATHS = ("src", "include", "tests", "cmake", "CMakeLists.txt")
CONFIG_KEYS = (
    "CMAKE_BUILD_TYPE",
    "CMAKE_CXX_COMPILER",
    "CMAKE_CXX_FLAGS_DEBUG",
    "DATAPUMP_SANITIZERS",
    "DATAPUMP_PORTABLE",
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def run_capture(command: list[str], cwd: Path, timeout: float | None = None) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=timeout, check=False)


def git(root: Path, *args: str) -> list[str]:
    return ["git", "-c", "safe.directory=" + str(root), *args]


def tracked_source_hashes(root: Path) -> dict[str, str]:
    result = run_capture(git(root, "ls-files", "-z", "--", *SOURCE_PATHS), root)
    if result.returncode:
        raise RuntimeError("git ls-files failed: " + result.stderr.decode("utf-8", "replace"))
    names = [name.decode("utf-8") for name in result.stdout.split(b"\0") if name]
    if not names:
        raise RuntimeError("source inventory is empty")
    hashes: dict[str, str] = {}
    for name in names:
        path = root / name
        if not path.is_file():
            raise RuntimeError("tracked source is missing or not a regular file: " + name)
        hashes[name] = sha256(path.read_bytes())
    return hashes


def cache_values(cache: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in cache.read_text().splitlines():
        match = re.match(r"^([^:#=]+):[^=]*=(.*)$", line)
        if match:
            values[match.group(1)] = match.group(2)
    return {key: values.get(key, "") for key in CONFIG_KEYS}


def source_registrations(source: Path) -> list[str]:
    text = source.read_text()
    names = re.findall(r'\brun\("((?:[^"\\]|\\.)*)"\s*,', text)
    if not names:
        raise RuntimeError("pattern receiver source registration inventory is empty")
    return [bytes(name, "utf-8").decode("unicode_escape") for name in names]


def ctest_stream_registration(root: Path, report: Path) -> dict:
    command = ["ctest", "--show-only=json-v1", "--test-dir", str(root / "build")]
    started_at = utc_now()
    started = time.monotonic()
    cpu_before = child_cpu_seconds()
    timed_out = False
    try:
        result = run_capture(command, root, timeout=30)
    except subprocess.TimeoutExpired as error:
        result = subprocess.CompletedProcess(command, 124, error.stdout or b"", error.stderr or b"")
        timed_out = True
    elapsed = time.monotonic() - started
    cpu_elapsed = max(0.0, child_cpu_seconds() - cpu_before)
    finished_at = utc_now()
    (report / "ctest-inventory.stdout.log").write_bytes(result.stdout)
    (report / "ctest-inventory.stderr.log").write_bytes(result.stderr)
    if result.returncode:
        raise RuntimeError("CTest inventory failed or timed out: " + result.stderr.decode("utf-8", "replace"))
    try:
        inventory = json.loads(result.stdout)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RuntimeError("CTest inventory was not valid JSON") from error
    matches = []
    for test in inventory.get("tests", []):
        command_text = " ".join(map(str, test.get("command", [])))
        if any(Path(str(part)).name == "test_stream_receive" for part in test.get("command", [])):
            matches.append({"name": test.get("name"), "command": test.get("command")})
        elif "test_stream_receive" in command_text:
            matches.append({"name": test.get("name"), "command": test.get("command")})
    if len(matches) != 1:
        raise RuntimeError("expected exactly one CTest registration for test_stream_receive; found " + str(len(matches)))
    return {"command": command, "started_at_utc": started_at, "finished_at_utc": finished_at,
            "wall_seconds": elapsed, "child_cpu_seconds": cpu_elapsed,
            "timeout_seconds": 30, "timed_out": timed_out, "exit_code": result.returncode,
            "stdout_file": "ctest-inventory.stdout.log", "stderr_file": "ctest-inventory.stderr.log",
            "stdout_sha256": sha256(result.stdout), "stderr_sha256": sha256(result.stderr),
            "registration": matches[0]}


def child_cpu_seconds() -> float:
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    return usage.ru_utime + usage.ru_stime


def run_child(name: str, command: list[str], root: Path, report: Path,
              timeout_seconds: float, source_before: dict[str, str]) -> dict:
    stdout_path = report / (name + ".stdout.log")
    stderr_path = report / (name + ".stderr.log")
    started_at = utc_now()
    started = time.monotonic()
    cpu_before = child_cpu_seconds()
    timed_out = False
    try:
        result = run_capture(command, root, timeout_seconds)
        stdout, stderr, exit_code = result.stdout, result.stderr, result.returncode
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or b""
        stderr = error.stderr or b""
        exit_code, timed_out = 124, True
    elapsed = time.monotonic() - started
    cpu_elapsed = max(0.0, child_cpu_seconds() - cpu_before)
    finished_at = utc_now()
    stdout_path.write_bytes(stdout)
    stderr_path.write_bytes(stderr)
    source_after = tracked_source_hashes(root)
    unchanged = source_after == source_before
    record = {
        "name": name,
        "command": command,
        "cwd": str(root),
        "started_at_utc": started_at,
        "finished_at_utc": finished_at,
        "wall_seconds": elapsed,
        "child_cpu_seconds": cpu_elapsed,
        "timeout_seconds": timeout_seconds,
        "timed_out": timed_out,
        "exit_code": exit_code,
        "stdout_file": stdout_path.name,
        "stderr_file": stderr_path.name,
        "stdout_sha256": sha256(stdout),
        "stderr_sha256": sha256(stderr),
        "source_unchanged_after_run": unchanged,
        "source_hashes_after_run": source_after,
    }
    if timed_out:
        record["failure"] = "component process exceeded its finite diagnostic timeout"
    elif exit_code != 0:
        record["failure"] = "component process returned nonzero"
    elif not unchanged:
        record["failure"] = "tracked source changed while component process ran"
    return record, stdout, stderr


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--expected-head", required=True)
    parser.add_argument("--timeout-seconds", type=float, default=120.0)
    args = parser.parse_args()

    root = args.root.resolve(strict=True)
    report = args.report.resolve()
    if args.timeout_seconds <= 0:
        parser.error("--timeout-seconds must be positive")
    top = run_capture(git(root, "rev-parse", "--show-toplevel"), root)
    if top.returncode or Path(top.stdout.decode().strip()).resolve() != root:
        raise RuntimeError("--root must be the Git checkout root")
    head_result = run_capture(git(root, "rev-parse", "HEAD"), root)
    if head_result.returncode:
        raise RuntimeError("could not read source HEAD")
    head = head_result.stdout.decode().strip()
    if head != args.expected_head:
        raise RuntimeError("source HEAD does not match caller-provided expected HEAD")

    # The report path is dedicated to this run. Refuse reuse to preserve prior evidence.
    report.mkdir(parents=True, exist_ok=False)
    try:
        cache = root / "build/CMakeCache.txt"
        cache_config = cache_values(cache)
        compiler = shutil.which("g++-11")
        expected_compiler = str(Path(compiler).resolve()) if compiler else ""
        actual_compiler = cache_config["CMAKE_CXX_COMPILER"]
        if actual_compiler and Path(actual_compiler).exists():
            actual_compiler = str(Path(actual_compiler).resolve())
        if cache_config["CMAKE_BUILD_TYPE"] != "Debug":
            raise RuntimeError("production component route requires Debug configuration")
        if cache_config["DATAPUMP_SANITIZERS"] != "ON":
            raise RuntimeError("production component route requires ASan/UBSan")
        if cache_config["DATAPUMP_PORTABLE"] != "OFF":
            raise RuntimeError("production component route requires DATAPUMP_PORTABLE=OFF")
        if not expected_compiler or actual_compiler != expected_compiler:
            raise RuntimeError("configured compiler is not the selected g++-11")

        binaries = {
            "test_pattern_receiver": root / "build/test_pattern_receiver",
            "test_stream_receive": root / "build/test_stream_receive",
            "test_live": root / "build/test_live",
            "test_live_planning_link": root / "build/test_live_planning_link",
        }
        for name, path in binaries.items():
            if not path.is_file() or not os.access(path, os.X_OK):
                raise RuntimeError("built test executable is missing or not executable: " + str(path))
        source_before = tracked_source_hashes(root)
        pattern_source = root / "tests/test_pattern_receiver.cpp"
        registrations = source_registrations(pattern_source)
        selector_inventory = {}
        for selector in PATTERN_SELECTORS:
            selected = [name for name in registrations if selector in name]
            if not selected:
                raise RuntimeError("no source registration matches focused selector: " + selector)
            selector_inventory[selector] = selected
        stream_registration = ctest_stream_registration(root, report)

        compiler_command = [expected_compiler, "--version"]
        compiler_result = run_capture(compiler_command, root, timeout=30)
        if compiler_result.returncode:
            raise RuntimeError("g++-11 --version failed")
        compiler_version = compiler_result.stdout.decode("utf-8", "replace").splitlines()[0]
        cache_bytes = cache.read_bytes()
        records = []
        for index, selector in enumerate(PATTERN_SELECTORS):
            command = [str(binaries["test_pattern_receiver"]), selector]
            record, stdout, _ = run_child("pattern-" + str(index + 1), command, root,
                                          report, args.timeout_seconds, source_before)
            passed = re.findall(rb"^([^\r\n:]+): passed\s*$", stdout, re.MULTILINE)
            actual_names = [name.decode("utf-8", "replace") for name in passed]
            expected_names = selector_inventory[selector]
            record["selector"] = selector
            record["expected_registered_names"] = expected_names
            record["observed_passed_names"] = actual_names
            record["registration_coverage"] = (len(actual_names) == len(set(actual_names)) and
                                               set(actual_names) == set(expected_names))
            if not record["registration_coverage"]:
                record["failure"] = "selector output did not cover its complete source registration inventory"
            records.append(record)
            if record.get("failure"):
                break

        if len(records) == len(PATTERN_SELECTORS) and all(not item.get("failure") for item in records):
            command = [str(binaries["test_stream_receive"])]
            record, stdout, _ = run_child("stream-receive", command, root, report,
                                          args.timeout_seconds, source_before)
            record["ctest_registration"] = stream_registration["registration"]
            record["registration_coverage"] = b"stream receive integration passed" in stdout
            if not record["registration_coverage"]:
                record["failure"] = "no-argument stream_receive suite completion marker missing"
            records.append(record)

        if len(records) == len(PATTERN_SELECTORS) + 1 and all(not item.get("failure") for item in records):
            command = [str(binaries["test_live_planning_link"])]
            record, stdout, _ = run_child("live-planning-link", command, root, report,
                                          args.timeout_seconds, source_before)
            record["registration_coverage"] = b"provider-free Live allocation/planning link regression passed" in stdout
            if not record["registration_coverage"]:
                record["failure"] = "no-argument Live planning/allocation suite completion marker missing"
            records.append(record)

        final_source = tracked_source_hashes(root)
        source_unchanged = final_source == source_before
        identity = {
            "classification": "focused production-source component checks; diagnostic only",
            "source_commit": head,
            "expected_source_commit": args.expected_head,
            "source_hashes_before": source_before,
            "source_hashes_after": final_source,
            "source_unchanged": source_unchanged,
            "build_cache_sha256": sha256(cache_bytes),
            "build_configuration": cache_config,
            "compiler_path": expected_compiler,
            "compiler_version_command": compiler_command,
            "compiler_version": compiler_version,
            "compiler_version_exit": compiler_result.returncode,
            "build_targets": ["test_live", "test_pattern_receiver", "test_stream_receive", "test_live_planning_link"],
            "runtime_environment": {key: os.environ[key] for key in
                                    ("ASAN_OPTIONS", "UBSAN_OPTIONS", "DATAPUMP_RX_CPU_PROFILE")
                                    if key in os.environ},
            "executables": {name: {"path": str(path), "sha256": sha256(path.read_bytes())}
                            for name, path in binaries.items()},
            "pattern_registration_inventory": selector_inventory,
            "stream_ctest_inventory": stream_registration,
            "timeout_seconds_per_child": args.timeout_seconds,
            "components": records,
            "all_components_passed": (len(records) == len(PATTERN_SELECTORS) + 2 and
                                       source_unchanged and all(not item.get("failure") for item in records)),
        }
        (report / "run.json").write_text(json.dumps(identity, indent=2, sort_keys=True) + "\n")
        print(json.dumps({"all_components_passed": identity["all_components_passed"],
                          "components_run": [item["name"] for item in records],
                          "report": str(report)}, sort_keys=True), flush=True)
        return 0 if identity["all_components_passed"] else 1
    except BaseException as error:
        (report / "setup-failure.json").write_text(json.dumps({
            "source_commit": head,
            "expected_source_commit": args.expected_head,
            "error_type": type(error).__name__,
            "error": str(error),
            "timestamp_utc": utc_now(),
        }, indent=2, sort_keys=True) + "\n")
        print(f"production component diagnostic setup failed: {type(error).__name__}: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

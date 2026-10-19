#!/usr/bin/env python3
"""Run and retain one original Live first-transition diagnostic capture."""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import resource
import runpy
import shutil
import subprocess
import sys
import time

TIMEOUT_SECONDS = 600
MAX_LOG_BYTES = 64 * 1024 * 1024
TRACE_NAME = "first-transition.jsonl"
SAFE_ENV = ("PATH", "LANG", "LC_ALL", "TZ", "ASAN_OPTIONS", "UBSAN_OPTIONS",
            "RUNNER_OS", "RUNNER_ARCH", "DATAPUMP_RX_CPU_PROFILE",
            "DATAPUMP_FFT_EXECUTION_INNER_CPU", "DATAPUMP_CONTINUATION_INNER_CPU",
            "DATAPUMP_FIRST_TRANSITION_CENSUS")

def sha_bytes(data): return hashlib.sha256(data).hexdigest()
def sha_file(path): return sha_bytes(path.read_bytes())
def write_json(path, value): path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
def env_digest(env):
    packed = b"\0".join(k.encode() + b"=" + v.encode(errors="surrogateescape")
                       for k, v in sorted(env.items()))
    return sha_bytes(packed)
def cache_data(path):
    raw = path.read_bytes()
    lines = raw.decode("utf-8", errors="replace").splitlines()
    keys = ("CMAKE_BUILD_TYPE", "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER",
            "CMAKE_C_COMPILER_ID", "CMAKE_C_COMPILER_VERSION", "CMAKE_GENERATOR",
            "CMAKE_CXX_FLAGS", "CMAKE_CXX_FLAGS_DEBUG", "CMAKE_CXX_FLAGS_RELEASE",
            "CMAKE_CXX_COMPILER_ID", "CMAKE_CXX_COMPILER_VERSION",
            "DATAPUMP_SANITIZERS", "DATAPUMP_PORTABLE")
    values = {}
    for key in keys:
        found = [line.split("=", 1)[1] for line in lines if line.startswith(key + ":")]
        if found:
            values[key] = found[0] if len(found) == 1 else {"duplicate_values": found}
    return raw, values

def load_source_identity(compiled_source):
    metadata_root = compiled_source
    source_root = compiled_source / "source" if (compiled_source / "source").is_dir() else compiled_source
    prepared_path = metadata_root / "prepared-source.json"
    inventory_path = compiled_source / "source-inventory.json"
    prepared = json.loads(prepared_path.read_text())
    inventory = json.loads(inventory_path.read_text())
    files = inventory["files_sha256"]
    observed = {}
    for relative, expected in files.items():
        path = source_root / relative
        if not path.is_file():
            raise ValueError(f"compiled source inventory file missing: {relative}")
        observed[relative] = sha_file(path)
        if observed[relative] != expected:
            raise ValueError(f"compiled source changed since preparation: {relative}")
    inventory_digest = sha_bytes(json.dumps(files, sort_keys=True, separators=(",", ":")).encode())
    if inventory_digest != inventory["inventory_sha256"]:
        raise ValueError("source inventory digest mismatch")
    return prepared, inventory, observed, source_root

def capture(root, compiled_source, binary, report):
    root = root.resolve(strict=True)
    compiled_source = compiled_source.resolve(strict=True)
    binary = binary.resolve(strict=True)
    report = report.absolute()
    if report.exists():
        raise FileExistsError(f"refusing to overwrite report: {report}")
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise ValueError("binary must exist and be executable")
    prepared, inventory, source_hashes, source_root = load_source_identity(compiled_source)
    cache_path = binary.parent / "CMakeCache.txt"
    if not cache_path.is_file():
        raise ValueError(f"expected CMakeCache.txt beside binary: {cache_path}")
    cache_raw, cache_values = cache_data(cache_path)
    if "CMAKE_CXX_COMPILER" not in cache_values:
        raise ValueError("CMake cache does not identify C++ compiler")
    cxx = Path(cache_values["CMAKE_CXX_COMPILER"])
    cxx_path = shutil.which(str(cxx)) if not cxx.is_absolute() else str(cxx)
    if not cxx_path or not Path(cxx_path).is_file():
        raise ValueError("configured C++ compiler is not available")
    compiler_probe = subprocess.run([cxx_path, "--version"], cwd=root,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if compiler_probe.returncode != 0:
        raise ValueError(f"configured compiler --version failed ({compiler_probe.returncode})")
    trace_validator = root / "tools/gui-stop-differential.py"
    if not trace_validator.is_file():
        raise ValueError("trace_seal validator is missing from root checkout")
    trace_seal = runpy.run_path(str(trace_validator))["trace_seal"]
    sanitizer_diagnostic = runpy.run_path(str(root / "tools/run-gui-smoke.py"))["SANITIZER_DIAGNOSTIC"]
    binary_sha_before = sha_file(binary)
    cache_sha_before = sha_bytes(cache_raw)
    compiler_sha = sha_file(Path(cxx_path))
    report.mkdir(parents=True, exist_ok=False)
    trace_path = (report / TRACE_NAME).resolve()
    command = [str(binary), "--simulation-media-clock-trace", str(trace_path)]
    child_env = os.environ.copy()
    child_env["DATAPUMP_FIRST_TRANSITION_CENSUS"] = "1"
    safe_environment = {key: child_env[key] for key in SAFE_ENV if key in child_env}
    environment_sha = env_digest(child_env)
    started_utc = datetime.now(timezone.utc).isoformat()
    started_mono = time.monotonic()
    cpu_before = resource.getrusage(resource.RUSAGE_CHILDREN)
    timed_out = False
    try:
        completed = subprocess.run(command, cwd=root, env=child_env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   timeout=TIMEOUT_SECONDS, check=False)
        stdout, stderr = completed.stdout, completed.stderr
        actual_exit = completed.returncode
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or b""
        stderr = error.stderr or b""
        actual_exit = None
        timed_out = True
    elapsed = time.monotonic() - started_mono
    cpu_after = resource.getrusage(resource.RUSAGE_CHILDREN)
    finished_utc = datetime.now(timezone.utc).isoformat()
    output_overflow = len(stdout) > MAX_LOG_BYTES or len(stderr) > MAX_LOG_BYTES
    (report / "stdout.log").write_bytes(stdout[:MAX_LOG_BYTES])
    (report / "stderr.log").write_bytes(stderr[:MAX_LOG_BYTES])
    trace_validation = None
    trace_error = None
    try:
        trace_validation = trace_seal(trace_path, stderr.decode("utf-8", errors="strict"))
    except (OSError, UnicodeError, ValueError, KeyError, TypeError) as error:
        trace_error = f"{type(error).__name__}: {error}"
    trace_complete = trace_validation is not None and not timed_out and not output_overflow
    cache_after, cache_values_after = cache_data(cache_path)
    binary_sha_after = sha_file(binary)
    source_after = {relative: sha_file(source_root / relative) for relative in source_hashes}
    source_unchanged = source_after == source_hashes
    sanitizer_report = bool(sanitizer_diagnostic.search(stdout.decode("utf-8", errors="replace") + "\n" + stderr.decode("utf-8", errors="replace")))
    identity_unchanged = (source_unchanged and sha_bytes(cache_after) == cache_sha_before and binary_sha_after == binary_sha_before)
    capture_ok = (not sanitizer_report and not timed_out and not output_overflow and actual_exit == 0 and
                  trace_complete and source_unchanged and
                  sha_bytes(cache_after) == cache_sha_before and binary_sha_after == binary_sha_before)
    functional_failure = (not sanitizer_report and not timed_out and actual_exit == 1 and
                          trace_complete and identity_unchanged)
    record = {
        "schema": 1,
        "scope": "one Symbol_Burst first-transition/coverage-limiter capture on original Live fixture",
        "classification": "diagnostic only; never Live/GUI qualification",
        "fixture": "one unchanged compiled test_live --simulation-media-clock-trace process; natural input; no paired timing comparison",
        "fixture_boundary": "production Symbol_Burst receiver through the existing test_live simulation boundary; original fixture assertions and 120-second internal functional limit remain in force",
        "source": {"baseline_source_commit": prepared["baseline_source_commit"],
                   "launcher_source_commit": prepared["launcher_source_commit"],
                   "compiled_source_directory": str(source_root),
                   "prepared_source_sha256": sha_file(compiled_source / "prepared-source.json"),
                   "inventory_sha256": sha_file(compiled_source / "source-inventory.json"),
                   "files_sha256": source_hashes, "unchanged_after_run": source_unchanged},
        "binary": {"path": str(binary), "sha256_before": binary_sha_before,
                   "sha256_after": binary_sha_after, "unchanged": binary_sha_before == binary_sha_after},
        "configuration": {"cache_path": str(cache_path), "cache_sha256_before": cache_sha_before,
                           "cache_sha256_after": sha_bytes(cache_after), "values": cache_values,
                           "values_after": cache_values_after,
                           "unchanged": sha_bytes(cache_after) == cache_sha_before},
        "compiler": {"path": str(Path(cxx_path).resolve()), "sha256": compiler_sha,
                     "version_exit": compiler_probe.returncode,
                     "version_stdout": compiler_probe.stdout.decode("utf-8", errors="replace")[:8192],
                     "version_stderr": compiler_probe.stderr.decode("utf-8", errors="replace")[:8192]},
        "environment": {"explicit_safe_values": safe_environment,
                        "full_environment_sha256": environment_sha,
                        "secret_values_recorded": False},
        "command": command, "working_directory": str(root),
        "started_utc": started_utc, "finished_utc": finished_utc,
        "wall_seconds": elapsed,
        "child_cpu_seconds": {"user": cpu_after.ru_utime - cpu_before.ru_utime,
                               "system": cpu_after.ru_stime - cpu_before.ru_stime},
        "timeout_seconds": TIMEOUT_SECONDS, "timed_out": timed_out,
        "actual_exit_code": actual_exit,
        "process_result": ("sanitizer_failure" if sanitizer_report else
                           "passed_original_functional_assertions" if capture_ok else
                           "functional_failure" if functional_failure else
                           "external_timeout" if timed_out else "process_failure"),
        "stdout": {"path": "stdout.log", "bytes_observed": len(stdout),
                   "sha256_observed": sha_bytes(stdout), "truncated": len(stdout) > MAX_LOG_BYTES},
        "stderr": {"path": "stderr.log", "bytes_observed": len(stderr),
                   "sha256_observed": sha_bytes(stderr), "truncated": len(stderr) > MAX_LOG_BYTES},
        "trace": {"path": str(trace_path), "exists": trace_path.is_file(),
                  "sha256": sha_file(trace_path) if trace_path.is_file() else None,
                  "bytes": trace_path.stat().st_size if trace_path.is_file() else None,
                  "validator": "tools/gui-stop-differential.py::trace_seal",
                  "validation": trace_validation, "validation_error": trace_error,
                  "complete": trace_complete},
        "output_overflow": output_overflow, "sanitizer_report": sanitizer_report,
        "identity_unchanged": identity_unchanged,
        "capture_status": "complete_diagnostic_capture" if capture_ok else
                          "complete_trace_functional_failure" if functional_failure and source_unchanged else
                          "incomplete_or_failed_capture",
        "qualification_claim": False,
    }
    write_json(report / "capture-report.json", record)
    if capture_ok:
        return 0
    return 1

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path, help="checkout containing tools/gui-stop-differential.py")
    parser.add_argument("--compiled-source", required=True, type=Path, help="private source tree from sourceprep.py")
    parser.add_argument("--binary", required=True, type=Path, help="compiled test_live executable")
    parser.add_argument("--report", required=True, type=Path, help="new report directory")
    args = parser.parse_args()
    try:
        result = capture(args.root, args.compiled_source, args.binary, args.report)
    except Exception as error:
        print(f"capture setup failed: {type(error).__name__}: {error}", file=sys.stderr)
        return 2
    return result

if __name__ == "__main__":
    raise SystemExit(main())

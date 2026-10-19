#!/usr/bin/env python3
"""Capture six sequential fixed-bank diagnostic replays; never qualification."""
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
import time

APPLICATION_COMMIT = "81c7da84121be60fe0ce1e7252e94cdf952773e8"
EXPECTED_PCM_SHA256 = "3f1ecd6bb83ebfd9b1314d25b7b1fcc77f8bf282503171bb2e0e59c921e6ddd1"
CLASSIFICATION = "diagnostic_prefix_replay_only"
EXPECTED_FIXTURE = {
    "generated_samples": 154676,
    "committed_prefix_samples": 154038,
    "partitions": 233,
    "original_run": 39269648667,
    "original_job": 118118268914,
}
EXPECTED_ORACLE_SHA256 = '48d2c0b5f0c11867fec2dec8ae7adcc489cb8cebfe8570899672892da74a91da'
SCOPES = ("0", "1", "0", "1", "0", "1")


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n")


def checked_path(base: Path, relative: str) -> Path:
    item = Path(relative)
    if item.is_absolute() or ".." in item.parts or not item.parts:
        raise ValueError("unsafe relative source path: " + relative)
    result = (base / item).resolve(strict=True)
    if not result.is_relative_to(base.resolve(strict=True)) or not result.is_file():
        raise ValueError("overlay path escapes compiled source or is not a file: " + relative)
    return result


def validate_integrated_source(root: Path, report: Path) -> tuple[dict, Path, Path]:
    source_report = report / "integrated-source.json"
    metadata = json.loads(source_report.read_text())
    if metadata.get("launcher_source_commit") != subprocess.check_output(
            ["git", "-c", "safe.directory=" + str(root), "rev-parse", "HEAD"], cwd=root, text=True).strip():
        raise ValueError("integrated launcher HEAD differs from current checkout")
    if metadata.get("application_source_commit") != APPLICATION_COMMIT:
        raise ValueError("integrated application source identity differs")
    if metadata.get("classification") != CLASSIFICATION:
        raise ValueError("integrated source classification differs")
    source = Path(metadata["compiled_source_directory"]).resolve(strict=True)
    if not source.is_dir():
        raise ValueError("compiled source directory is not a directory")
    overlay = metadata.get("compiled_overlay_sha256")
    if not isinstance(overlay, dict) or not overlay:
        raise ValueError("compiled overlay inventory is missing")
    for relative, expected in overlay.items():
        if not isinstance(relative, str) or not isinstance(expected, str) or len(expected) != 64:
            raise ValueError("malformed compiled overlay inventory entry")
        if sha(checked_path(source, relative).read_bytes()) != expected:
            raise ValueError("compiled overlay hash mismatch: " + relative)
    fixture = metadata.get("fixture")
    if not isinstance(fixture, dict) or any(fixture.get(k) != v for k, v in EXPECTED_FIXTURE.items()):
        raise ValueError("fixed-bank fixture metadata differs from accepted identity")
    pcm_path = Path(fixture["pcm_path"]).resolve(strict=True)
    pcm_bytes = pcm_path.read_bytes()
    if (len(pcm_bytes) != EXPECTED_FIXTURE["generated_samples"] * 4 or
            fixture.get("pcm_sha256") != EXPECTED_PCM_SHA256 or sha(pcm_bytes) != EXPECTED_PCM_SHA256):
        raise ValueError("fixed-bank PCM length or independently pinned hash mismatch")
    for path_key, hash_key, expected in (
        ("sidecar_path", "sidecar_sha256", "afe4936afe0241d23823384e3f0c4b3f7c6dcfe21587401996362e83cb126a9d"),
        ("oracle_path", "oracle_sha256", EXPECTED_ORACLE_SHA256)):
        path = Path(fixture[path_key]).resolve(strict=True)
        if sha(path.read_bytes()) != expected or fixture.get(hash_key) != expected:
            raise ValueError("fixed replay input identity differs: " + path_key)
    return metadata, source, pcm_path


def validate_build(root: Path, source: Path) -> tuple[dict, Path, Path]:
    cache_path = root / "build/CMakeCache.txt"
    cache_lines = cache_path.read_text().splitlines()
    config = {}
    for key in ("CMAKE_BUILD_TYPE", "DATAPUMP_SANITIZERS", "DATAPUMP_PORTABLE", "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER", "CMAKE_HOME_DIRECTORY"):
        matches = [line.split("=", 1)[1] for line in cache_lines if line.startswith(key + ":")]
        if len(matches) != 1:
            raise ValueError("missing or duplicate CMake cache key: " + key)
        config[key] = matches[0]
    if (config["CMAKE_BUILD_TYPE"], config["DATAPUMP_SANITIZERS"], config["DATAPUMP_PORTABLE"]) != ("Debug", "ON", "OFF"):
        raise ValueError("acquisition partition diagnostic requires sanitized Debug, Portable OFF")
    if Path(config["CMAKE_HOME_DIRECTORY"]).resolve(strict=True) != source:
        raise ValueError("configured CMake source differs from integrated source")
    c_compiler, compiler = shutil.which("gcc-11"), shutil.which("g++-11")
    if not c_compiler or not compiler or Path(config["CMAKE_C_COMPILER"]).resolve() != Path(c_compiler).resolve() or Path(config["CMAKE_CXX_COMPILER"]).resolve() != Path(compiler).resolve():
        raise ValueError("CMake cache must select gcc-11/g++-11")
    binary = root / "build/test_live"
    if not binary.is_file():
        raise ValueError("built test_live executable is missing")
    c_version = subprocess.run([c_compiler, "--version"], cwd=root, capture_output=True, text=True, check=False)
    cxx_version = subprocess.run([compiler, "--version"], cwd=root, capture_output=True, text=True, check=False)
    if c_version.returncode != 0 or cxx_version.returncode != 0 or not c_version.stdout.strip() or not cxx_version.stdout.strip():
        raise ValueError("gcc-11/g++-11 version probe failed")
    compiler_versions = {
        "c_compiler_path": str(Path(c_compiler).resolve()), "c_compiler_exit": c_version.returncode,
        "c_compiler_version": c_version.stdout.strip(),
        "cxx_compiler_path": str(Path(compiler).resolve()), "cxx_compiler_exit": cxx_version.returncode,
        "cxx_compiler_version": cxx_version.stdout.strip(),
    }
    return config, cache_path, binary, compiler_versions


def safe_environment() -> dict[str, str]:
    keys = ("PATH", "LANG", "LC_ALL", "TZ", "ASAN_OPTIONS", "UBSAN_OPTIONS", "RUNNER_OS", "RUNNER_ARCH")
    env = {key: os.environ[key] for key in keys if key in os.environ}
    env.update({
        "DATAPUMP_OWNER_CENSUS_DIAGNOSTIC": "1",
        "DATAPUMP_CAPACITY_SHAPE_DIAGNOSTIC": "1",
        "DATAPUMP_CAPACITY_CPU_DIAGNOSTIC": "1",
        "DATAPUMP_RX_CPU_PROFILE": "coarse",
    })
    return env


def run_one(*, root: Path, report: Path, binary: Path, pcm_path: Path, pcm_sha256: str,
            index: int, timeout_seconds: int, trace_validator) -> dict:
    label = f"case-{index + 1:02d}-{('off', 'on')[index % 2]}"
    out = report / "outputs" / label
    out.mkdir(parents=True, exist_ok=False)
    trace = (out / "trace.jsonl").resolve()
    command = [str(binary), "--capacity-fixed-bank-replay", str(pcm_path), str(trace)]
    env = safe_environment()
    env["DATAPUMP_ACQUISITION_INNER_CPU"] = SCOPES[index]
    # Replay input is supplied as argv and deliberately has no output-capture variable.
    env_digest = sha("\0".join(k + "=" + v for k, v in sorted(env.items())).encode())
    start_utc = datetime.now(timezone.utc).isoformat()
    wall_start = time.monotonic()
    cpu_start = resource.getrusage(resource.RUSAGE_CHILDREN)
    timed_out = False
    try:
        result = subprocess.run(command, cwd=root, env=env, capture_output=True,
                                timeout=timeout_seconds, check=False)
        stdout, stderr, exit_code = result.stdout, result.stderr, result.returncode
    except subprocess.TimeoutExpired as error:
        # subprocess.run waits for and reaps its timed-out child before returning.
        stdout, stderr, exit_code, timed_out = error.stdout or b"", error.stderr or b"", 124, True
    wall_seconds = time.monotonic() - wall_start
    cpu_end = resource.getrusage(resource.RUSAGE_CHILDREN)
    finish_utc = datetime.now(timezone.utc).isoformat()
    (out / "stdout.log").write_bytes(stdout)
    (out / "stderr.log").write_bytes(stderr)
    seal = seal_error = None
    try:
        seal = trace_validator(trace, stderr.decode("utf-8", errors="strict"))
    except (OSError, UnicodeError, ValueError, KeyError, TypeError) as error:
        seal_error = type(error).__name__ + ": " + str(error)
    input_unchanged = pcm_path.is_file() and sha(pcm_path.read_bytes()) == pcm_sha256
    record = {
        "index": index, "scope": SCOPES[index], "label": label,
        "command": command, "cwd": str(root), "environment": env, "environment_sha256": env_digest,
        "started_utc": start_utc, "finished_utc": finish_utc, "timeout_seconds": timeout_seconds,
        "timed_out": timed_out, "exit_code": exit_code, "wall_seconds": wall_seconds,
        "process_cpu_user_seconds": cpu_end.ru_utime - cpu_start.ru_utime,
        "process_cpu_system_seconds": cpu_end.ru_stime - cpu_start.ru_stime,
        "process_cpu_scope": "RUSAGE_CHILDREN delta for exactly this sequential subprocess, including child threads",
        "stdout_path": str(out / "stdout.log"), "stdout_sha256": sha(stdout),
        "stderr_path": str(out / "stderr.log"), "stderr_sha256": sha(stderr),
        "trace_path": str(trace), "trace_sha256": sha(trace.read_bytes()) if trace.is_file() else None,
        "trace_seal": seal, "trace_seal_error": seal_error,
        "input_path": str(pcm_path), "input_sha256": pcm_sha256, "input_unchanged_after_run": input_unchanged,
        "binary_path": str(binary), "binary_sha256": sha(binary.read_bytes()),
    }
    write_json(out / "run.json", record)
    return record


def semantic_readback(trace_path: Path, oracle_path: Path) -> dict:
    rows = [json.loads(line) for line in trace_path.read_text().splitlines()]
    oracle = json.loads(oracle_path.read_text())
    errors = []
    canonical = lambda event: {k: v for k, v in event.items() if k not in ("sequence", "steady")}
    names = ("receiver_selection", "receiver_bank_admitted", "receiver_bank_retired")
    timeline = [canonical(row) for row in rows if row.get("event") in names]
    if timeline != oracle["timeline"]:
        first = next((i for i, (a, b) in enumerate(zip(timeline, oracle["timeline"])) if a != b), min(len(timeline), len(oracle["timeline"])))
        errors.append({"timeline_first_difference": first, "actual_count": len(timeline), "expected_count": len(oracle["timeline"]),
                       "actual": timeline[first] if first < len(timeline) else None,
                       "expected": oracle["timeline"][first] if first < len(oracle["timeline"]) else None})
    ends = [row for row in rows if row.get("event") == "capacity_shape_fixed_bank_end"]
    if len(ends) != 1:
        errors.append({"end_record_count": len(ends)})
        end = {}
    else:
        end = ends[0]
    expected = {**oracle["end"], "bank_sample": 154038, "input_boundary": 154038,
                "committed_partitions": 233, "unfed_suffix_samples": 638, "revision": 1,
                "eof_injected": 0, "absence_injected": 0, "limited": 0, "coverage_limited": 0,
                "health_input_overruns": 0, "health_dropped_samples": 0, "health_input_interrupted": 0,
                "health_search_limited": 0, "health_receiver_reset": 0,
                "receiver_limit": "", "session_error": "", "session_receiver_error": ""}
    for key, value in expected.items():
        if end.get(key) != value:
            errors.append({"end_field": key, "actual": end.get(key), "expected": value})
    points = [row for row in rows if row.get("event") == "capacity_cpu_work" and
              row.get("boundary") == "joined_refresh_before_new_admissions"]
    point = points[-1] if points else {}
    for key, value in oracle["work"].items():
        if point.get(key) != value:
            errors.append({"work_field": key, "actual": point.get(key), "expected": value})
    parts = [row for row in rows if row.get("event") == "capacity_shape_fixed_bank_partition"]
    if [row.get("partition_index") for row in parts] != list(range(233)):
        errors.append({"partition_order_or_count": len(parts)})
    if not any(row.get("case_id") == 1 and row.get("physical_complete") == 1 and
               row.get("validated") == 0 and row.get("missing_symbols") == 0 and
               (row.get("text_prefix") if row.get("binary") else row.get("raw_bits_prefix")) == "001" for row in parts):
        errors.append({"raw_physical_complete_exact_001": False})
    if not any(row.get("case_id") == 2 and row.get("physical_complete") == 1 and
               row.get("missing_symbols") == 0 and row.get("text_prefix") == "hi" for row in parts):
        errors.append({"short_physical_complete_hi": False})
    profiles = [row for row in rows if row.get("event") == "receiver_component_cpu"]
    profile = profiles[-1] if len(profiles) == 1 else {}
    for key, value in {"cpu_profile_complete": 1, "cpu_profile_active_contexts": 0,
                       "cpu_profile_clock_failures": 0, "cpu_profile_nesting_failures": 0,
                       "cpu_profile_overflows": 0}.items():
        if profile.get(key) != value:
            errors.append({"cpu_field": key, "actual": profile.get(key), "expected": value})
    stage_ns = {k: v for k, v in profile.items() if k.startswith("cpu_") and k.endswith("_ns") and k != "cpu_profile_covered_ns"}
    if not stage_ns or sum(stage_ns.values()) != profile.get("cpu_profile_covered_ns"):
        errors.append({"exclusive_cpu_sum_mismatch": True})
    output = [canonical(row) for row in parts + ends]
    return {"passed": not errors, "errors": errors, "timeline_events": len(timeline),
            "committed_prefix_samples": end.get("bank_sample"), "partition_records": len(parts),
            "work": {k: point.get(k) for k in oracle["work"]}, "exclusive_stage_ns": stage_ns,
            "profile": profile, "output_sha256": sha(json.dumps(output, sort_keys=True, separators=(",", ":")).encode()),
            "scope": "exact recorded acquisition timeline/work plus bounded selected publication rows; not original replay/publication performance"}


def capture(root: Path, report: Path, timeout_seconds: int) -> dict:
    root, report = root.resolve(strict=True), report.resolve(strict=True)
    if timeout_seconds != 300:
        raise ValueError("per-process deadline is fixed at 300 seconds")
    if (report / "outputs").exists() or (report / "capture-summary.json").exists():
        raise ValueError("capture output paths must be fresh")
    integrated, source, pcm_path = validate_integrated_source(root, report)
    config, cache_path, binary, compiler_versions = validate_build(root, source)
    trace_validator = runpy.run_path(str(root / "tools/gui-stop-differential.py"))["trace_seal"]
    records, failure, baseline_output = [], None, None
    for index in range(6):
        try:
            record = run_one(root=root, report=report, binary=binary, pcm_path=pcm_path,
                             pcm_sha256=integrated["fixture"]["pcm_sha256"], index=index,
                             timeout_seconds=timeout_seconds, trace_validator=trace_validator)
        except Exception as error:
            failure = f"{type(error).__name__}: {error}"
            break
        if record["exit_code"] == 0 and record["trace_seal"] is not None:
            try:
                readback = semantic_readback(Path(record["trace_path"]), Path(integrated["fixture"]["oracle_path"]))
                if baseline_output is None and readback["passed"]:
                    baseline_output = readback["output_sha256"]
                if baseline_output is not None and readback["output_sha256"] != baseline_output:
                    readback["passed"] = False
                    readback["errors"].append({"matched_output_digest_differs": True})
                inner_ns = sum(v for k, v in readback["exclusive_stage_ns"].items() if k.startswith("cpu_ordinary_acq_"))
                if (record["scope"] == "0" and inner_ns != 0) or (record["scope"] == "1" and inner_ns <= 0):
                    readback["passed"] = False
                    readback["errors"].append({"inner_scope_switch_mismatch": inner_ns, "scope": record["scope"]})
                record["semantic_readback"] = readback
            except (OSError, ValueError, KeyError, TypeError) as error:
                record["semantic_readback"] = {"passed": False, "error": type(error).__name__ + ": " + str(error)}
        else:
            record["semantic_readback"] = {"passed": False, "error": "process/trace failed before fidelity readback"}
        write_json(Path(record["stdout_path"]).parent / "run.json", record)
        records.append(record)
        if record["timed_out"] or record["exit_code"] != 0 or record["trace_seal"] is None or not record["input_unchanged_after_run"] or not record["semantic_readback"]["passed"]:
            failure = "replay timed out, failed, had an unsealed trace, or modified fixed input, or failed exact replay fidelity"
            break
    summary = {
        "scope": "simulation-acquisition-partition-diagnostic",
        "classification": CLASSIFICATION,
        "interpretation": "six sequential fixed-bank diagnostic prefix replays; three alternating matched OFF/ON inner-CPU pairs; diagnostic only, not qualification or a performance guarantee",
        "launcher_source_commit": integrated["launcher_source_commit"],
        "application_source_commit": integrated["application_source_commit"],
        "compiled_source_directory": str(source),
        "compiled_overlay_sha256": integrated["compiled_overlay_sha256"],
        "integrated_source_sha256": sha((report / "integrated-source.json").read_bytes()),
        "fixture": integrated["fixture"], "cache_path": str(cache_path),
        "cache_sha256": sha(cache_path.read_bytes()), "configuration": config,
        "compiler_versions": compiler_versions,
        "binary_path": str(binary), "binary_sha256": sha(binary.read_bytes()),
        "runner_helper_path": str(Path(__file__).resolve()), "runner_helper_sha256": sha(Path(__file__).read_bytes()),
        "requested_runs": 6, "completed_runs": len(records), "per_process_timeout_seconds": timeout_seconds,
        "records": records, "failure": failure,
        "status": "captured_diagnostic_prefix_replay" if failure is None and len(records) == 6 else "incomplete_or_failed_diagnostic",
    }
    write_json(report / "capture-summary.json", summary)
    print(json.dumps(summary), flush=True)
    if failure is not None or len(records) != 6:
        raise SystemExit(1)
    return summary


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    capture_parser = sub.add_parser("capture")
    capture_parser.add_argument("--root", type=Path, required=True)
    capture_parser.add_argument("--report", type=Path, required=True)
    capture_parser.add_argument("--timeout-seconds", type=int, default=300)
    args = parser.parse_args()
    capture(args.root, args.report, args.timeout_seconds)


if __name__ == "__main__":
    main()

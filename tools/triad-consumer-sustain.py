#!/usr/bin/env python3
"""Run bounded sustained receiver timing replays from a performance index.

Repeated scans and observation copies are timing work on the same sampled
capture, never independent reception trials or admission calibration.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import traceback
from typing import Any


TELEMETRY_FIELDS = ("timestamp", "utilization.gpu", "utilization.memory",
                    "memory.used", "power.draw", "clocks.sm", "clocks.mem")
WORKSPACE_MIB = 8192


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def text_output(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value)


def selected_row(index: dict[str, Any], policy: str) -> dict[str, Any]:
    rows = [row for row in index.get("records", [])
            if row.get("stage") == "performance" and row.get("policy") == policy
            and row.get("status") == "completed" and isinstance(row.get("argv"), list)]
    if len(rows) != 1:
        raise ValueError(f"expected one completed performance row for {policy}; found {len(rows)}")
    return rows[0]


def set_option(argv: list[str], name: str, value: str) -> list[str]:
    result: list[str] = []
    index = 0
    found = False
    while index < len(argv):
        item = argv[index]
        if item == name:
            if index + 1 >= len(argv):
                raise ValueError(f"recorded argv has {name} without a value")
            result.extend((name, value))
            found = True
            index += 2
        else:
            result.append(item)
            index += 1
    if not found:
        result.extend((name, value))
    return result


def effective_argv(original: list[str], output: Path, repeats: int,
                   repeat_observations: int) -> list[str]:
    if len(original) < 3:
        raise ValueError("recorded workspace argv is too short")
    argv = set_option(original, "--output", str(output))
    argv = set_option(argv, "--repeats", str(repeats))
    argv = set_option(argv, "--repeat-observations", str(repeat_observations))
    # Hold the complete state workspace fixed at the campaign's declared 8192 MiB.
    argv = set_option(argv, "--workspace-memory-mib", str(WORKSPACE_MIB))
    return argv


def capture_path_from_argv(argv: list[str], root: Path) -> tuple[Path, str]:
    candidate = Path(argv[2])
    if not candidate.is_absolute():
        candidate = (root / candidate).resolve()
    if not candidate.is_dir() or not (candidate / "manifest.json").is_file():
        raise FileNotFoundError("recorded argv input is not an available sampled-capture directory")
    return candidate, candidate.name


def input_identity(capture: Path, row: dict[str, Any]) -> dict[str, Any]:
    manifest = capture / "manifest.json"
    return {"capture_basename": capture.name,
            "manifest_sha256": sha256(manifest) if manifest.is_file() else None,
            "file_hashes_from_performance_row": row.get("input_hashes"),
            "source_fingerprints_from_performance_row": row.get("source_fingerprints"),
            "source_hashes_from_performance_index": row.get("source_hashes")}


def gpu_index(argv: list[str]) -> int | None:
    for index, token in enumerate(argv[:-1]):
        if token == "--device":
            device = argv[index + 1]
            match = re.fullmatch(r"cuda(?::(\d+))?", device)
            if not match:
                return None
            return int(match.group(1) or 0)
    return None


def start_gpu_telemetry(index: int | None, output_dir: Path):
    executable = shutil.which("nvidia-smi")
    csv_path = output_dir / "gpu-telemetry.csv"
    error_path = output_dir / "gpu-telemetry.stderr.txt"
    if executable is None or index is None:
        reason = "nvidia-smi unavailable" if executable is None else "selected device ordinal unavailable"
        (output_dir / "gpu-telemetry-unavailable.txt").write_text(reason + "\n")
        return None, None, {"status": "unavailable", "reason": reason,
                            "fields": list(TELEMETRY_FIELDS)}
    command = [executable, "-i", str(index),
               "--query-gpu=" + ",".join(TELEMETRY_FIELDS),
               "--format=csv,noheader,nounits", "-l", "1"]
    csv_stream = csv_path.open("w", buffering=1)
    error_stream = error_path.open("w", buffering=1)
    try:
        process = subprocess.Popen(command, stdout=csv_stream, stderr=error_stream,
                                   text=True, close_fds=True)
    except OSError as error:
        csv_stream.close()
        error_stream.write(f"nvidia-smi launch error: {type(error).__name__}\n")
        error_stream.close()
        return None, None, {"status": "unavailable", "reason": f"launch failed: {type(error).__name__}",
                            "fields": list(TELEMETRY_FIELDS),
                            "csv_file": csv_path.name, "stderr_file": error_path.name}
    return process, (csv_stream, error_stream), {
        "status": "running", "fields": list(TELEMETRY_FIELDS),
        "sampling_interval_seconds": 1, "device_ordinal": index,
        "command_query_fields_only": True,
        "csv_file": csv_path.name, "stderr_file": error_path.name}


def stop_gpu_telemetry(process: Any, streams: Any, metadata: dict[str, Any]) -> None:
    if process is not None:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        metadata["returncode"] = process.returncode
        metadata["stopped_and_waited"] = True
    if streams:
        for stream in streams:
            stream.flush()
            stream.close()
    if metadata.get("status") == "running":
        metadata["status"] = "stopped"


def cgroup_cpu_sample() -> dict[str, Any]:
    """Read aggregate CPU usage/quota without exposing cgroup paths."""
    usage_ns = None
    source = None
    v2_stat = Path("/sys/fs/cgroup/cpu.stat")
    try:
        pairs = dict(line.split() for line in v2_stat.read_text().splitlines() if len(line.split()) == 2)
        if "usage_usec" in pairs:
            usage_ns = int(pairs["usage_usec"]) * 1000
            source = "cgroup-v2"
    except (OSError, ValueError):
        pass
    if usage_ns is None:
        for path in (Path("/sys/fs/cgroup/cpuacct/cpuacct.usage"),
                     Path("/sys/fs/cgroup/cpu,cpuacct/cpuacct.usage")):
            try:
                usage_ns = int(path.read_text().strip())
                source = "cgroup-v1"
                break
            except (OSError, ValueError):
                continue
    quota_cores = None
    v2_quota = Path("/sys/fs/cgroup/cpu.max")
    try:
        quota, period = v2_quota.read_text().split()
        if quota != "max" and int(quota) > 0 and int(period) > 0:
            quota_cores = int(quota) / int(period)
        source = source or "cgroup-v2"
    except (OSError, ValueError):
        for quota_path, period_path in (
            (Path("/sys/fs/cgroup/cpu/cpu.cfs_quota_us"), Path("/sys/fs/cgroup/cpu/cpu.cfs_period_us")),
            (Path("/sys/fs/cgroup/cpu,cpuacct/cpu.cfs_quota_us"), Path("/sys/fs/cgroup/cpu,cpuacct/cpu.cfs_period_us")),
        ):
            try:
                quota, period = int(quota_path.read_text()), int(period_path.read_text())
                if quota > 0 and period > 0:
                    quota_cores = quota / period
                source = source or "cgroup-v1"
                break
            except (OSError, ValueError):
                continue
    return {"usage_cpu_nanoseconds": usage_ns, "quota_cores": quota_cores,
            "cgroup_version": source, "scope": "entire cgroup/container"}


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def run(args: argparse.Namespace) -> dict[str, Any]:
    root = args.root.resolve()
    output_dir = args.output_dir.resolve()
    if output_dir.exists():
        raise FileExistsError("output directory must be new")
    try:
        output_dir.relative_to(root)
    except ValueError as error:
        raise ValueError("output directory must be under --root") from error
    index_path = args.index.resolve()
    if not index_path.is_file():
        raise FileNotFoundError("performance index is unavailable")
    index = json.loads(index_path.read_text())
    row = selected_row(index, args.policy)
    original_argv = row["argv"]
    capture, _ = capture_path_from_argv(original_argv, root)
    workspace_source = Path(original_argv[1])
    if not workspace_source.is_absolute():
        workspace_source = (root / workspace_source).resolve()
    current_source_hashes = {"triad-consumer-sustain.py": sha256(Path(__file__).resolve())}
    if workspace_source.is_file():
        current_source_hashes[workspace_source.name] = sha256(workspace_source)
    output_dir.mkdir(parents=True)
    run_output = output_dir / "sustained-receiver.json"
    argv = effective_argv(original_argv, run_output, args.repeats, args.repeat_observations)
    stdout_path = output_dir / "receiver.stdout.txt"
    stderr_path = output_dir / "receiver.stderr.txt"
    index_hash = sha256(index_path)
    result = {"schema": "triad-consumer-sustain-v1", "status": "running",
        "started_utc": utc_now(), "policy": args.policy,
        "source_identity": {"index_basename": index_path.name,
            "index_sha256": index_hash,
            "index_source_revision": index.get("source_revision"),
            "selected_performance_row_source_hashes": row.get("source_hashes"),
            "selected_performance_row_source_fingerprints": row.get("source_fingerprints"),
            "current_source_hashes": current_source_hashes},
        "input_identity": input_identity(capture, row),
        "recorded_campaign_argv": original_argv,
        "effective_argv": argv,
        "replay_contract": {"repeats": args.repeats,
            "repeat_observations": args.repeat_observations,
            "workspace_memory_mib": WORKSPACE_MIB,
            "physical_sampled_captures": 1,
            "repeats_and_observation_copies_are_independent_trials": False,
            "classification": "timing-only replay of the same sampled capture",
            "complete_receiver_finish_included": True},
        "timeout_seconds": args.timeout,
        "gpu_telemetry": {}, "cpu_cgroup": {},
        "stdout_file": stdout_path.name, "stderr_file": stderr_path.name,
        "result_file": run_output.name}
    result_path = output_dir / "sustain.json"
    write_json(result_path, result)
    gpu_proc = None
    gpu_streams = None
    telemetry = None
    start_mono = time.perf_counter()
    start_cpu = cgroup_cpu_sample()
    returncode = None
    stdout = ""
    stderr = ""
    timed_out = False
    try:
        gpu_proc, gpu_streams, telemetry = start_gpu_telemetry(gpu_index(argv), output_dir)
        result["gpu_telemetry"] = telemetry
        run_start_utc = utc_now()
        completed = subprocess.run(argv, cwd=root, capture_output=True, text=True,
                                   timeout=args.timeout, check=False)
        returncode = completed.returncode
        stdout, stderr = completed.stdout, completed.stderr
        result["status"] = "completed" if returncode == 0 else "failed"
    except subprocess.TimeoutExpired as error:
        timed_out = True
        returncode = None
        stdout, stderr = text_output(error.stdout), text_output(error.stderr)
        stderr += f"\nTimeout after {args.timeout} seconds.\n"
        result["status"] = "timeout"
    except Exception as error:
        stdout, stderr = "", f"{type(error).__name__}: {error}\n{traceback.format_exc()}"
        result["status"] = "failed_to_launch"
    finally:
        stop_gpu_telemetry(gpu_proc, gpu_streams, telemetry if telemetry is not None else {})
        end_cpu = cgroup_cpu_sample()
        wall = time.perf_counter()-start_mono
    stdout_path.write_text(stdout)
    stderr_path.write_text(stderr)
    result["run_started_utc"] = locals().get("run_start_utc")
    result["finished_utc"] = utc_now()
    result["wall_seconds"] = wall
    result["timed_out"] = timed_out
    result["returncode"] = returncode
    result["stdout_sha256"] = sha256(stdout_path)
    result["stderr_sha256"] = sha256(stderr_path)
    cpu_delta = None
    cpu_cores = None
    before_usage = start_cpu.get("usage_cpu_nanoseconds")
    after_usage = end_cpu.get("usage_cpu_nanoseconds")
    if before_usage is not None and after_usage is not None and after_usage >= before_usage and wall > 0:
        cpu_delta = after_usage-before_usage
        cpu_cores = cpu_delta / 1e9 / wall
    result["cpu_cgroup"] = {"before": start_cpu, "after": end_cpu,
        "delta_cpu_nanoseconds": cpu_delta, "average_cpu_cores_used": cpu_cores,
        "average_cpu_cores_used_scope": "entire cgroup/container, including telemetry sampler"}
    result["gpu_telemetry"] = telemetry or {"status": "unavailable"}
    result["receiver_result"] = None
    if run_output.is_file():
        try:
            payload = json.loads(run_output.read_text())
            result["receiver_result"] = {"sha256": sha256(run_output),
                "schema": payload.get("schema"), "timing": payload.get("timing"),
                "search": payload.get("search"), "precision": payload.get("precision"),
                "source_hashes": payload.get("source_hashes"),
                "input_hashes": payload.get("input_hashes"),
                "admission_calibration": payload.get("precision", {}).get("admission_calibration")
                    if isinstance(payload.get("precision"), dict) else None}
        except (OSError, json.JSONDecodeError) as error:
            result["receiver_result_error"] = f"{type(error).__name__}: {error}"
    result["sampling_note"] = "nvidia-smi telemetry is one-second polling and can miss short transients"
    write_json(result_path, result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--index", type=Path, required=True,
                        help="run-campaign performance index; may be outside --root")
    parser.add_argument("--output-dir", type=Path, required=True,
                        help="new result directory under --root")
    parser.add_argument("--policy", choices=("fp32", "tensor:fp16"), required=True)
    parser.add_argument("--repeats", type=int, default=4000)
    parser.add_argument("--repeat-observations", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=300)
    args = parser.parse_args()
    if not 1 <= args.repeats <= 10000 or not 1 <= args.repeat_observations <= 128:
        parser.error("repeats must be 1..10000 and repeat-observations must be 1..128")
    if not 1 <= args.timeout <= 3600:
        parser.error("timeout must be between 1 and 3600 seconds")
    try:
        result = run(args)
    except Exception as error:
        parser.error(f"sustain setup failed: {type(error).__name__}: {error}")
    print(json.dumps({"status": result["status"],
                      "result": str(args.output_dir.resolve() / "sustain.json"),
                      "returncode": result.get("returncode"),
                      "wall_seconds": result.get("wall_seconds")}, sort_keys=True))
    return 0 if result["status"] == "completed" else 1


if __name__ == "__main__":
    raise SystemExit(main())

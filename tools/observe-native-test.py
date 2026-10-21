#!/usr/bin/env python3
"""Passively sample one target ELF in a driver's descendant tree."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import signal
import subprocess
import sys
import time


MIB = 1024 * 1024
SAMPLE_NS = 100_000_000
OUTPUT_CAP = 16 * MIB
MAX_THREADS = 128
MAX_PROCESSES = 4096
MAX_LOG_MARKERS = 4096
ENV_ALLOWLIST = (
    "PATH", "LD_LIBRARY_PATH", "LANG", "LC_ALL", "DISPLAY",
    "WAYLAND_DISPLAY", "OMP_NUM_THREADS", "QT_QPA_PLATFORM",
)
SOURCE_HASH_FILES = ("tests/test_symbol_burst_live.cpp", "src/live.cpp",
                     "src/search_parallel.cpp", "tools/run-ci-tests.py")


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            block = stream.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def proc_stat(pid):
    fields = (Path("/proc") / str(pid) / "stat").read_text().rsplit(")", 1)[1].split()
    return {
        "state": fields[0],
        "user_ticks": int(fields[11]),
        "system_ticks": int(fields[12]),
        "start_ticks": int(fields[19]),
    }


def proc_children(pid):
    base = Path("/proc") / str(pid) / "task"
    try:
        tasks = sorted(base.iterdir(), key=lambda item: int(item.name))
    except (FileNotFoundError, ProcessLookupError, PermissionError):
        return [], None
    if len(tasks) > MAX_THREADS:
        return [], "process thread inventory exceeds 128-thread traversal cap"
    children = set()
    errors = []
    for task in tasks:
        try:
            raw = (task / "children").read_text().split()
            children.update(int(item) for item in raw)
        except (FileNotFoundError, ProcessLookupError):
            continue
        except OSError as error:
            errors.append(type(error).__name__)
    return sorted(children), ("children read errors: " + ",".join(errors) if errors else None)


def descendants(root_pid):
    pending = [root_pid]
    visited = set()
    rows = []
    errors = []
    while pending:
        pid = pending.pop()
        if pid in visited:
            continue
        visited.add(pid)
        if len(visited) > MAX_PROCESSES:
            errors.append("descendant tree exceeds 4096-process cap")
            break
        try:
            identity = proc_stat(pid)
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            continue
        rows.append((pid, identity))
        children, error = proc_children(pid)
        if error:
            errors.append(f"pid {pid}: {error}")
        pending.extend(child for child in children if child not in visited)
    return rows, errors


def priority(tid):
    try:
        return os.getpriority(os.PRIO_PROCESS, tid)
    except (OSError, ProcessLookupError):
        return None


def affinity(tid):
    try:
        return sorted(os.sched_getaffinity(tid))
    except (OSError, ProcessLookupError):
        return None


def sample_target(pid):
    base = Path("/proc") / str(pid)
    record = proc_stat(pid)
    record.update({"pid": pid, "affinity": affinity(pid), "nice": priority(pid), "threads": []})
    try:
        tasks = sorted((base / "task").iterdir(), key=lambda item: int(item.name))
    except (FileNotFoundError, ProcessLookupError, PermissionError):
        record["thread_inventory_unavailable"] = True
        return record
    if len(tasks) > MAX_THREADS:
        raise RuntimeError("target thread inventory exceeds 128-thread cap")
    for task in tasks:
        tid = int(task.name)
        try:
            thread = {"tid": tid, **proc_stat(tid), "affinity": affinity(tid), "nice": priority(tid)}
            try:
                thread["wchan"] = (task / "wchan").read_text().strip()[:120]
            except OSError as error:
                thread["wchan_error"] = type(error).__name__
            record["threads"].append(thread)
        except (FileNotFoundError, ProcessLookupError):
            record.setdefault("departed_threads", []).append(tid)
    try:
        for line in (base / "status").read_text().splitlines():
            if line.startswith("VmRSS:"):
                record["rss_kib"] = int(line.split()[1])
                break
    except OSError as error:
        record["status_error"] = type(error).__name__
    return record


def json_line(stream, record, budget):
    raw = json.dumps(record, separators=(",", ":")).encode() + b"\n"
    if budget[0] + len(raw) > OUTPUT_CAP - 4096:
        return False
    stream.write(raw)
    budget[0] += len(raw)
    return True


def build_metadata(binary, command, source, log_path):
    root = Path.cwd()
    source_hashes = {}
    for name in SOURCE_HASH_FILES:
        path = root / name
        if path.is_file():
            source_hashes[name] = sha256_file(path)
    caches = []
    for parent in (binary.parent, *list(binary.parents)[:4]):
        candidate = parent / "CMakeCache.txt"
        if candidate.is_file():
            values = {}
            for line in candidate.read_text(errors="replace").splitlines():
                if line.startswith(("CMAKE_BUILD_TYPE:", "CMAKE_CXX_COMPILER:", "CMAKE_C_COMPILER:",
                                    "CMAKE_GENERATOR:", "CMAKE_MAKE_PROGRAM:")):
                    key, value = line.split("=", 1)
                    values[key] = value
            caches.append({"path": str(candidate), "sha256": sha256_file(candidate), "selected": values})
            break
    metadata = {
        "schema": 1,
        "target_binary": str(binary),
        "target_sha256": sha256_file(binary),
        "target_elf": binary.read_bytes()[:4] == b"\x7fELF",
        "source_commit": source,
        "source_file_sha256": source_hashes,
        "command": command,
        "cwd": str(root),
        "selected_environment": {key: os.environ[key] for key in ENV_ALLOWLIST if key in os.environ},
        "build_cache": caches,
        "log_path": None if log_path is None else str(log_path),
        "log_sha256_before": sha256_file(log_path) if log_path is not None and log_path.is_file() else None,
        "clock_ticks_per_second": os.sysconf("SC_CLK_TCK"),
        "sample_period_ns": SAMPLE_NS,
        "output_cap_bytes": OUTPUT_CAP,
        "thread_cap": MAX_THREADS,
        "process_tree_cap": MAX_PROCESSES,
    }
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--source", default=None, help="source commit or descriptive revision")
    parser.add_argument("--log", type=Path, help="existing driver log to follow for progress markers")
    parser.add_argument("command", nargs=argparse.REMAINDER,
                        help="original driver command after --")
    args = parser.parse_args()
    command = args.command[1:] if args.command and args.command[0] == "--" else args.command
    if not command:
        parser.error("provide the unchanged driver command after --")
    binary = args.binary.resolve(strict=True)
    output = args.output.resolve()
    log_path = None if args.log is None else args.log.resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error("--binary must name an executable file")
    if binary.read_bytes()[:4] != b"\x7fELF":
        parser.error("--binary is not an ELF image")
    output.mkdir(parents=True, exist_ok=False)

    retained = output / "retained-target.elf"
    if binary.stat().st_size >= OUTPUT_CAP - MIB:
        raise ValueError("target ELF leaves insufficient space within output cap")
    shutil.copy2(binary, retained)
    if sha256_file(binary) != sha256_file(retained) or not os.access(retained, os.X_OK):
        raise RuntimeError("retained ELF hash or executable mode differs")
    metadata = build_metadata(binary, command, args.source, log_path)
    metadata["retained_binary"] = {"path": retained.name, "sha256": sha256_file(retained),
                                   "bytes": retained.stat().st_size}
    metadata["inherited_affinity"] = affinity(0)
    metadata["inherited_nice"] = priority(0)
    target_sha = metadata["target_sha256"]
    metadata_path = output / "metadata.json"
    samples_path = output / "samples.jsonl"
    events_path = output / "events.jsonl"
    observation_path = output / "observation.json"
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
    budget = [metadata_path.stat().st_size + retained.stat().st_size]
    errors = []
    warnings = []
    seen_targets = {}
    departures = []
    target_sample_count = 0
    total_sample_count = 0
    tree_sample_count = 0
    max_tree_size = 0
    sample_gaps = 0
    max_sample_gap_ns = 0
    last_sample_ns = None
    log_offset = 0
    log_markers = 0
    if log_path is not None and log_path.is_file():
        try:
            with log_path.open("rb") as log_stream:
                log_offset = log_stream.tell()
        except OSError as error:
            errors.append("cannot open driver log: " + str(error))
    started_ns = time.monotonic_ns()
    driver = None
    return_code = None
    before = resource.getrusage(resource.RUSAGE_SELF)
    log_stream = None
    try:
        with samples_path.open("xb") as samples, events_path.open("xb") as events:
            if log_path is not None and log_path.is_file():
                try:
                    log_stream = log_path.open("rb")
                    log_stream.seek(log_offset)
                except OSError as error:
                    errors.append("cannot follow driver log: " + str(error))
            driver = subprocess.Popen(command)
            next_sample_ns = started_ns
            while driver.poll() is None:
                now_ns = time.monotonic_ns()
                if now_ns >= next_sample_ns:
                    total_sample_count += 1
                    if last_sample_ns is not None:
                        gap = now_ns - last_sample_ns
                        max_sample_gap_ns = max(max_sample_gap_ns, gap)
                        if gap > SAMPLE_NS * 1.5:
                            sample_gaps += 1
                    last_sample_ns = now_ns
                    tree, tree_errors = descendants(driver.pid)
                    tree_sample_count += 1
                    max_tree_size = max(max_tree_size, len(tree))
                    for error in tree_errors:
                        if error not in errors and len(errors) < 32:
                            errors.append(error)
                    matches = []
                    for pid, identity in tree:
                        try:
                            actual = os.readlink(Path("/proc") / str(pid) / "exe")
                        except (FileNotFoundError, ProcessLookupError, PermissionError):
                            continue
                        if actual.endswith(" (deleted)"):
                            actual = actual[:-10]
                        try:
                            actual_path = str(Path(actual).resolve())
                        except OSError:
                            actual_path = actual
                        if actual_path == str(binary):
                            target_identity = (pid, identity["start_ticks"])
                            if target_identity not in seen_targets:
                                seen_targets[target_identity] = now_ns
                                event = {"kind": "target_discovered", "elapsed_ns": now_ns - started_ns,
                                         "pid": pid, "start_ticks": identity["start_ticks"],
                                         "binary_sha256": target_sha}
                                if not json_line(events, event, budget):
                                    errors.append("diagnostic output reached 16MiB cap")
                                    break
                            matches.append((pid, identity))
                    if len(matches) > 1 and "multiple simultaneous target ELF processes found" not in errors:
                        errors.append("multiple simultaneous target ELF processes found")
                    for pid, _ in matches:
                        try:
                            record = {"elapsed_ns": now_ns - started_ns, **sample_target(pid)}
                            if not json_line(samples, record, budget):
                                if "diagnostic output reached 16MiB cap" not in errors:
                                    errors.append("diagnostic output reached 16MiB cap")
                                break
                            target_sample_count += 1
                        except (FileNotFoundError, ProcessLookupError):
                            departures.append({"pid": pid, "elapsed_ns": now_ns - started_ns,
                                               "reason": "departed during sample"})
                        except Exception as error:
                            message = "target sample failed: " + type(error).__name__ + ": " + str(error)
                            if message not in errors and len(errors) < 32:
                                errors.append(message)
                    if log_stream is None and log_path is not None and log_path.is_file():
                        log_stream = log_path.open("rb")
                        log_stream.seek(log_offset)
                    if log_markers >= MAX_LOG_MARKERS and "log marker cap reached" not in errors:
                        errors.append("log marker cap reached")
                    if log_stream is not None and log_markers < MAX_LOG_MARKERS:
                        try:
                            for line in log_stream:
                                line = line.rstrip(b"\r\n")
                                text = line.decode("utf-8", "replace")
                                lower = text.lower()
                                if ("timestamp" in lower or "progress" in lower or "frontier" in lower
                                        or "queue" in lower or "drain" in lower or "gui_application" in lower):
                                    event = {"kind": "log_marker", "elapsed_ns": now_ns - started_ns,
                                             "line": text[:2000]}
                                    if not json_line(events, event, budget):
                                        if "diagnostic output reached 16MiB cap" not in errors:
                                            errors.append("diagnostic output reached 16MiB cap")
                                        break
                                    log_markers += 1
                            log_offset = log_stream.tell()
                        except OSError as error:
                            if len(errors) < 32:
                                errors.append("driver log follow failed: " + str(error))
                    next_sample_ns = now_ns + SAMPLE_NS
                time.sleep(0.005)
            return_code = driver.wait()
            if log_stream is not None:
                log_stream.close()
                log_stream = None
            finished_ns = time.monotonic_ns()
            for identity, first_seen_ns in seen_targets.items():
                pid, start_ticks = identity
                try:
                    current = proc_stat(pid)
                    if current["start_ticks"] == start_ticks:
                        departures.append({"pid": pid, "start_ticks": start_ticks,
                                           "first_seen_elapsed_ns": first_seen_ns - started_ns,
                                           "reason": "still present after driver exit"})
                    else:
                        departures.append({"pid": pid, "start_ticks": start_ticks,
                                           "first_seen_elapsed_ns": first_seen_ns - started_ns,
                                           "reason": "pid reused after target departure"})
                except (FileNotFoundError, ProcessLookupError, PermissionError):
                    departures.append({"pid": pid, "start_ticks": start_ticks,
                                       "first_seen_elapsed_ns": first_seen_ns - started_ns,
                                       "reason": "target departed"})
            if not seen_targets:
                errors.append("target ELF was never observed in the driver's descendant tree")
            if len(seen_targets) != 1:
                errors.append(f"expected exactly one target ELF identity; observed {len(seen_targets)}")
            if target_sample_count == 0:
                errors.append("target ELF had no complete process/thread samples")
            if departures and any(item["reason"] == "still present after driver exit" for item in departures):
                errors.append("target ELF remains after the driver exited")
            after = resource.getrusage(resource.RUSAGE_SELF)
            observation = {
                "schema": 1,
                "driver_command": command,
                "driver_pid": driver.pid,
                "driver_exit_code": return_code,
                "driver_reaped": driver.poll() is not None,
                "observer_error": errors,
                "warnings": warnings,
                "diagnostics_complete": not errors,
                "target_identity_count": len(seen_targets),
                "target_identities": [{"pid": pid, "start_ticks": start_ticks}
                                      for pid, start_ticks in seen_targets],
                "target_sample_count": target_sample_count,
                "tree_sample_count": tree_sample_count,
                "total_sample_count": total_sample_count,
                "maximum_tree_size": max_tree_size,
                "sample_gaps_over_150ms": sample_gaps,
                "maximum_sample_gap_ns": max_sample_gap_ns,
                "log_marker_count": log_markers,
                "log_bytes_read": log_offset,
                "target_departures": departures,
                "wall_seconds": (finished_ns - started_ns) / 1e9,
                "observer_cpu_seconds": (after.ru_utime + after.ru_stime) - (before.ru_utime + before.ru_stime),
                "evidence_bytes_before_summary": budget[0],
                "output_cap_bytes": OUTPUT_CAP,
                "target_binary_sha256": target_sha,
                "finished_monotonic_ns": finished_ns,
            }
            raw = json.dumps(observation, indent=2).encode() + b"\n"
            if budget[0] + len(raw) <= OUTPUT_CAP:
                observation_path.write_bytes(raw)
            else:
                observation["observer_error"].append("summary would exceed 16MiB output cap")
                observation["diagnostics_complete"] = False
                observation_path.write_text(json.dumps(observation, indent=2) + "\n")
    except BaseException as error:
        if driver is not None and driver.poll() is None:
            # Do not kill or signal a correctly running test because observation
            # failed. Continue waiting and preserve the driver's real result.
            try:
                return_code = driver.wait()
            except Exception:
                return_code = driver.poll()
        if log_stream is not None:
            log_stream.close()
        emergency = {
            "schema": 1,
            "driver_command": command,
            "driver_pid": None if driver is None else driver.pid,
            "driver_exit_code": return_code,
            "driver_reaped": driver is not None and driver.poll() is not None,
            "observer_error": ["observer aborted: " + type(error).__name__ + ": " + str(error)],
            "diagnostics_complete": False,
            "target_binary_sha256": target_sha,
            "output_cap_bytes": OUTPUT_CAP,
        }
        try:
            observation_path.write_text(json.dumps(emergency, indent=2) + "\n")
        except OSError:
            pass
    return 2 if return_code is None else return_code


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print("observe-native-test: " + type(error).__name__ + ": " + str(error), file=sys.stderr)
        sys.exit(2)

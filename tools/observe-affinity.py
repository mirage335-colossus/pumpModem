#!/usr/bin/env python3
"""Run one retained ELF case and sample child process/thread scheduling state."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import selectors
import signal
import subprocess
import sys
import time


def stat_record(path):
    fields = path.read_text().rsplit(")", 1)[1].split()
    return {"state": fields[0], "user_ticks": int(fields[11]),
            "system_ticks": int(fields[12]), "start_ticks": int(fields[19])}


def affinity_record(pid):
    try:
        return sorted(os.sched_getaffinity(pid))
    except (OSError, ProcessLookupError):
        return None


def thread_priority(tid):
    try:
        return os.getpriority(os.PRIO_PROCESS, tid)
    except (OSError, ProcessLookupError):
        return None


def sample_process(pid):
    base = Path("/proc") / str(pid)
    value = stat_record(base / "stat")
    value["pid"] = pid
    value["affinity"] = affinity_record(pid)
    value["nice"] = thread_priority(pid)
    value["threads"] = []
    tasks = sorted((base / "task").iterdir(), key=lambda item: int(item.name))
    if len(tasks) > 128:
        raise RuntimeError("thread inventory exceeds 128-thread cap")
    for task in tasks:
        tid = int(task.name)
        try:
            row = {"tid": tid, **stat_record(task / "stat"),
                   "affinity": affinity_record(tid), "nice": thread_priority(tid)}
            try:
                row["wchan"] = (task / "wchan").read_text().strip()[:120]
            except OSError as exc:
                row["wchan_unavailable"] = type(exc).__name__
            value["threads"].append(row)
        except FileNotFoundError:
            value.setdefault("departed_during_sample", []).append(tid)
    for line in (base / "status").read_text().splitlines():
        if line.startswith("VmRSS:"):
            value["rss_kib"] = int(line.split()[1])
    return value


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("affinity", help="comma-separated logical CPU ids")
    parser.add_argument("cwd", type=Path)
    args = parser.parse_args()
    binary, output, cwd = args.binary.resolve(), args.output.resolve(), args.cwd.resolve()
    expected = sorted({int(item) for item in args.affinity.split(",")})
    if not expected or len(expected) != len(args.affinity.split(",")):
        raise ValueError("requested CPU affinity must be a nonempty unique list")
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise ValueError("retained ELF is missing or not executable")
    output.mkdir()
    os.setpriority(os.PRIO_PROCESS, 0, 10)
    env = os.environ.copy()
    for name in list(env):
        if name.startswith("LD_"):
            del env[name]
    env["LD_LIBRARY_PATH"] = str(binary.parent / "runtime")
    started = time.monotonic_ns()
    result = {
        "command": [str(binary)], "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "requested_affinity": expected, "requested_nice": 10,
        "sample_period_ns": 100_000_000, "clock_ticks_per_second": os.sysconf("SC_CLK_TCK"),
        "start_monotonic_ns": started, "observer_error": None, "sample_count": 0,
        "event_count": 0, "affinity_verified": False, "nice_verified": False,
    }
    child = None
    encoded_bytes = 0

    def set_child_affinity():
        os.sched_setaffinity(0, set(expected))
        applied = sorted(os.sched_getaffinity(0))
        nice = os.getpriority(os.PRIO_PROCESS, 0)
        os.write(affinity_write_fd, json.dumps({"affinity": applied, "nice": nice}).encode() + b"\n")
        os.close(affinity_write_fd)
        if applied != expected:
            raise RuntimeError("child pre-exec affinity differs from requested mask")

    def interrupted(signum, frame):
        raise KeyboardInterrupt("signal " + str(signum))

    signal.signal(signal.SIGTERM, interrupted)
    before = resource.getrusage(resource.RUSAGE_SELF)
    try:
        with (output / "samples.jsonl").open("xb") as samples, \
                (output / "events.jsonl").open("xb") as events:
            affinity_read_fd, affinity_write_fd = os.pipe()
            child = subprocess.Popen([str(binary)], stdin=subprocess.DEVNULL, stdout=None,
                                     stderr=subprocess.PIPE, env=env, cwd=cwd,
                                     preexec_fn=set_child_affinity, pass_fds=(affinity_write_fd,))
            os.close(affinity_write_fd)
            receipt = bytearray()
            while b"\n" not in receipt and len(receipt) <= 256:
                block = os.read(affinity_read_fd, 256 - len(receipt))
                if not block:
                    break
                receipt.extend(block)
            os.close(affinity_read_fd)
            if b"\n" not in receipt:
                raise RuntimeError("child pre-exec affinity receipt missing")
            applied = json.loads(bytes(receipt).split(b"\n", 1)[0])
            result["preexec_affinity"] = applied.get("affinity")
            result["preexec_nice"] = applied.get("nice")
            result["affinity_verified"] = result["preexec_affinity"] == expected
            result["nice_verified"] = result["preexec_nice"] == 10
            result["pid"] = child.pid
            streams = selectors.DefaultSelector()
            streams.register(child.stderr, selectors.EVENT_READ)
            pending = b""
            next_sample = time.monotonic()
            while streams.get_map() or child.poll() is None:
                now = time.monotonic()
                if now >= next_sample and child.poll() is None:
                    try:
                        record = {"elapsed_ns": time.monotonic_ns() - started,
                                  **sample_process(child.pid)}
                        if result["sample_count"] == 0:
                            result["first_sample_affinity"] = record["affinity"]
                            result["first_sample_nice"] = record["nice"]
                        row = json.dumps(record, separators=(",", ":")).encode() + b"\n"
                        encoded_bytes += len(row)
                        if encoded_bytes > 8 * MIB:
                            raise RuntimeError("sample/event evidence exceeded 8MiB cap")
                        samples.write(row)
                        result["sample_count"] += 1
                    except FileNotFoundError:
                        result["exit_during_sample"] = True
                    next_sample = now + 0.1
                for key, _ in streams.select(0.025):
                    block = os.read(key.fileobj.fileno(), 65536)
                    if not block:
                        streams.unregister(key.fileobj)
                        key.fileobj.close()
                        continue
                    sys.stderr.buffer.write(block)
                    sys.stderr.buffer.flush()
                    pending += block
                    if len(pending) > MIB:
                        raise RuntimeError("unterminated stderr line exceeded 1MiB cap")
                    while b"\n" in pending:
                        line, pending = pending.split(b"\n", 1)
                        event = {"elapsed_ns": time.monotonic_ns() - started,
                                 "line": line.decode("utf-8", "replace")}
                        try:
                            event["process"] = stat_record(Path("/proc") / str(child.pid) / "stat")
                        except FileNotFoundError:
                            event["process_exited"] = True
                        row = json.dumps(event, separators=(",", ":")).encode() + b"\n"
                        encoded_bytes += len(row)
                        if encoded_bytes > 8 * MIB:
                            raise RuntimeError("sample/event evidence exceeded 8MiB cap")
                        events.write(row)
                        result["event_count"] += 1
            if pending:
                events.write(json.dumps({"elapsed_ns": time.monotonic_ns() - started,
                                         "unterminated_line": pending.decode("utf-8", "replace")}).encode() + b"\n")
            streams.close()
            result["exit_code"] = child.wait(timeout=2)
    except (Exception, KeyboardInterrupt) as exc:
        result["observer_error"] = str(exc)
        if child is not None and child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=2)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=2)
        result["exit_code"] = None if child is None else child.returncode
    finally:
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
        own = resource.getrusage(resource.RUSAGE_SELF)
        result.update({"wall_seconds": (time.monotonic_ns() - started) / 1e9,
                       "child_user_seconds": after.ru_utime,
                       "child_system_seconds": after.ru_stime,
                       "child_max_rss_kib": after.ru_maxrss,
                       "observer_cpu_seconds": own.ru_utime + own.ru_stime - before.ru_utime - before.ru_stime,
                       "evidence_bytes": encoded_bytes,
                       "child_reaped": child is not None and child.poll() is not None})
        (output / "observation.json").write_text(json.dumps(result, indent=2) + "\n")
    if result["observer_error"] or not result["affinity_verified"] or not result["nice_verified"]:
        return 2
    return result["exit_code"] if result["exit_code"] in range(256) else 2


MIB = 1024 * 1024

if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print("observe-affinity: " + type(exc).__name__ + ": " + str(exc), file=sys.stderr)
        sys.exit(2)

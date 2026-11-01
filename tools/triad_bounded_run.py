#!/usr/bin/env python3
"""Run one isolated command in an owned process group with bounded cleanup.

This is a small POSIX/Linux launcher for long-running simulation campaigns.
It never invokes a shell and never reports success until every process in the
child's process group has exited and been reaped. Descendants that detach into
another session are outside this launcher's ownership contract. The declared
maximum applies to command runtime; bounded TERM/KILL cleanup can add at most
`2 * max(1, term_grace) + min(1, term_grace)` seconds.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import math
import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import time
from typing import Any


PR_SET_CHILD_SUBREAPER = 36
MAX_DECLARED_SECONDS = 7 * 24 * 60 * 60
MAX_TERM_GRACE_SECONDS = 30.0
POLL_SECONDS = 0.025
EXIT_TIMEOUT = 124
EXIT_SETUP = 125


class RunnerError(RuntimeError):
    pass


def _positive_seconds(text: str) -> float:
    try:
        value = float(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("expected a finite positive number of seconds") from exc
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError("expected a finite positive number of seconds")
    return value


def _enable_subreaper() -> None:
    """Adopt and reap orphaned descendants on Linux before launching the group."""
    if os.name != "posix":
        raise RunnerError("unsupported platform: a POSIX process-group API is required")
    if not sys.platform.startswith("linux"):
        raise RunnerError("unsupported platform: descendant reaping currently requires Linux")
    try:
        libc = ctypes.CDLL(None, use_errno=True)
        prctl = libc.prctl
        prctl.argtypes = (ctypes.c_int, ctypes.c_ulong, ctypes.c_ulong,
                          ctypes.c_ulong, ctypes.c_ulong)
        prctl.restype = ctypes.c_int
        if prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0:
            error = ctypes.get_errno()
            raise OSError(error, os.strerror(error))
    except (AttributeError, OSError) as exc:
        raise RunnerError(f"cannot enable Linux child reaping: {exc}") from exc


def _group_exists(pgid: int) -> bool:
    try:
        os.killpg(pgid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        # A process in the owned group that we cannot signal is still present.
        return True


def _reap_adopted_group_children(pgid: int, direct_pid: int) -> int:
    """Reap any exited descendants adopted by this process as subreaper."""
    reaped = 0
    proc = pathlib.Path("/proc")
    try:
        candidates = list(proc.iterdir())
    except OSError:
        return 0
    for entry in candidates:
        if not entry.name.isdecimal():
            continue
        pid = int(entry.name)
        if pid == direct_pid:
            # Popen must own/reap the direct child itself.
            continue
        try:
            stat = (entry / "stat").read_text()
            close = stat.rfind(")")
            fields = stat[close + 2:].split()  # state, ppid, pgrp, ...
            if close < 0 or int(fields[1]) != os.getpid() or int(fields[2]) != pgid:
                continue
            waited_pid, _ = os.waitpid(pid, os.WNOHANG)
        except ChildProcessError:
            continue
        except InterruptedError:
            continue
        except (OSError, ValueError, IndexError):
            continue
        if waited_pid > 0:
            reaped += 1
    return reaped


def _wait_group_empty(pgid: int, direct_process: subprocess.Popen[bytes], seconds: float) -> bool:
    deadline = time.monotonic() + seconds
    while True:
        # Popen owns the direct child; poll it before reaping only orphaned
        # descendants adopted through PR_SET_CHILD_SUBREAPER.
        direct_process.poll()
        _reap_adopted_group_children(pgid, direct_process.pid)
        if not _group_exists(pgid):
            return True
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return False
        time.sleep(min(POLL_SECONDS, remaining))


def _wait_direct_child_reaped(process: subprocess.Popen[bytes], seconds: float) -> bool:
    """Use bounded WNOHANG polling; never wait indefinitely after escalation."""
    deadline = time.monotonic() + seconds
    while True:
        if process.poll() is not None:
            return True
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return process.poll() is not None
        time.sleep(min(POLL_SECONDS, remaining))


def _terminate_owned_group(pgid: int, direct_process: subprocess.Popen[bytes],
                           grace_seconds: float) -> dict[str, Any]:
    """TERM the owned group, escalate to KILL, then reap all adopted children."""
    sent_term = False
    sent_kill = False
    if _group_exists(pgid):
        try:
            os.killpg(pgid, signal.SIGTERM)
            sent_term = True
        except ProcessLookupError:
            pass
    empty = _wait_group_empty(pgid, direct_process, grace_seconds)
    if not empty:
        # Check immediately before signalling to reduce the process-group reuse
        # window. POSIX killpg has no pidfd-style atomic identity check, so a
        # narrow reuse race cannot be eliminated between this check and signal.
        if _group_exists(pgid):
            try:
                os.killpg(pgid, signal.SIGKILL)
                sent_kill = True
            except ProcessLookupError:
                pass
        # SIGKILL cannot be caught. Allow a short bounded interval for kernel
        # exit and reap every orphan that this process adopted.
        empty = _wait_group_empty(pgid, direct_process, max(1.0, grace_seconds))
    return {"term_sent": sent_term, "kill_sent": sent_kill,
            "group_empty": empty,
            "direct_child_reaped": direct_process.poll() is not None}


def _write_status(path: pathlib.Path, result: dict[str, Any]) -> None:
    """Write the final small result atomically; never overwrite an old run."""
    if path.exists():
        raise RunnerError(f"status file already exists: {path}")
    payload = (json.dumps(result, indent=2, sort_keys=True) + "\n").encode("utf-8")
    fd, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp",
                                          dir=path.parent)
    temporary = pathlib.Path(temporary_name)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.link(temporary, path)
    except BaseException:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        raise
    else:
        temporary.unlink()


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cwd", required=True, type=pathlib.Path,
                        help="existing working directory for the command")
    parser.add_argument("--log", required=True, type=pathlib.Path,
                        help="new combined stdout/stderr log file")
    parser.add_argument("--status", required=True, type=pathlib.Path,
                        help="new JSON result path")
    parser.add_argument("--timeout", required=True, type=_positive_seconds,
                        help="maximum command runtime in seconds (cleanup has a separate bounded allowance)")
    parser.add_argument("--max-seconds", required=True, type=_positive_seconds,
                        help="declared run ceiling; --timeout must not exceed it")
    parser.add_argument("--term-grace", type=_positive_seconds, default=5.0,
                        help=f"seconds to wait after TERM before KILL (default: 5; maximum: {MAX_TERM_GRACE_SECONDS:g})")
    parser.add_argument("command", nargs=argparse.REMAINDER,
                        help="command and arguments, preceded by --")
    return parser


def run(args: argparse.Namespace) -> int:
    if args.timeout > args.max_seconds:
        raise RunnerError("--timeout exceeds the declared --max-seconds limit")
    if args.max_seconds > MAX_DECLARED_SECONDS:
        raise RunnerError(f"--max-seconds cannot exceed {MAX_DECLARED_SECONDS:g}")
    if args.term_grace > MAX_TERM_GRACE_SECONDS:
        raise RunnerError(f"--term-grace cannot exceed {MAX_TERM_GRACE_SECONDS:g}")
    command = list(args.command)
    if command and command[0] == "--":
        command.pop(0)
    if not command or not command[0]:
        raise RunnerError("provide a command after --")

    cwd = args.cwd.expanduser().resolve(strict=True)
    if not cwd.is_dir():
        raise RunnerError(f"--cwd is not a directory: {cwd}")
    log = args.log.expanduser().resolve()
    status = args.status.expanduser().resolve()
    if not log.parent.is_dir() or not status.parent.is_dir():
        raise RunnerError("log and status parent directories must already exist")
    if log == status:
        raise RunnerError("--log and --status must name different files")
    if log.exists():
        raise RunnerError(f"log file already exists: {log}")
    if status.exists():
        raise RunnerError(f"status file already exists: {status}")

    _enable_subreaper()
    started = time.monotonic()
    received_signal: int | None = None
    old_handlers: dict[int, Any] = {}

    def on_signal(signum: int, _frame: object) -> None:
        nonlocal received_signal
        if received_signal is None:
            received_signal = signum

    for signum in (signal.SIGINT, signal.SIGTERM):
        old_handlers[signum] = signal.signal(signum, on_signal)

    process: subprocess.Popen[bytes] | None = None
    pgid: int | None = None
    termination: dict[str, Any] | None = None
    timed_out = False
    setup_error: str | None = None
    child_returncode: int | None = None
    outcome = "setup_error"
    exit_code = EXIT_SETUP
    log_created = False
    try:
        # Exclusive creation prevents a stale partial log from being mistaken
        # for output of this run.
        with log.open("xb") as stream:
            log_created = True
            process = subprocess.Popen(command, cwd=cwd, stdin=subprocess.DEVNULL,
                                       stdout=stream, stderr=subprocess.STDOUT,
                                       close_fds=True, start_new_session=True)
            pgid = process.pid
            deadline = started + args.timeout
            while process.poll() is None and received_signal is None:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    timed_out = True
                    break
                time.sleep(min(POLL_SECONDS, remaining))
            if process.poll() is None:
                outcome = "cancelled" if received_signal is not None else "timeout"
                termination = _terminate_owned_group(pgid, process, args.term_grace)
                termination["direct_child_reaped"] = _wait_direct_child_reaped(
                    process, min(1.0, args.term_grace))
                termination["direct_wait_completed"] = True
                if termination["direct_child_reaped"]:
                    child_returncode = process.returncode
            else:
                child_returncode = process.returncode
                # A command that exits while leaving workers behind is a
                # failure, even when those workers can be safely cleaned up.
                if _group_exists(pgid):
                    termination = _terminate_owned_group(pgid, process, args.term_grace)
                    outcome = "left_descendants"
                else:
                    termination = {"term_sent": False, "kill_sent": False,
                                   "group_empty": True}
                    outcome = "completed"

        if received_signal is not None:
            outcome = "cancelled"
            exit_code = 128 + received_signal
        elif timed_out:
            outcome = "timeout"
            exit_code = EXIT_TIMEOUT
        elif outcome == "left_descendants":
            exit_code = EXIT_SETUP
        elif child_returncode is not None and child_returncode < 0:
            outcome = "child_signal"
            exit_code = 128 + -child_returncode
        else:
            exit_code = int(child_returncode or 0)
            outcome = "completed" if exit_code == 0 else "child_failure"
    except FileExistsError as exc:
        setup_error = str(exc)
        outcome = "setup_error"
        exit_code = EXIT_SETUP
    except OSError as exc:
        setup_error = f"{type(exc).__name__}: {exc}"
        outcome = "setup_error"
        exit_code = EXIT_SETUP
        if process is not None and pgid is not None:
            termination = _terminate_owned_group(pgid, process, args.term_grace)
            termination["direct_child_reaped"] = _wait_direct_child_reaped(
                process, min(1.0, args.term_grace))
            termination["direct_wait_completed"] = True
            if termination["direct_child_reaped"]:
                child_returncode = process.returncode
    finally:
        # Do not let a second Ctrl-C or TERM interrupt bounded cleanup/status
        # recording and strand the group that this invocation owns.
        for signum in old_handlers:
            signal.signal(signum, signal.SIG_IGN)
        if process is not None and pgid is not None:
            if process.poll() is None or _group_exists(pgid):
                if termination is None:
                    termination = _terminate_owned_group(pgid, process, args.term_grace)
                if not termination.get("direct_wait_completed", False):
                    direct_reaped = _wait_direct_child_reaped(
                        process, min(1.0, args.term_grace))
                    termination["direct_child_reaped"] = direct_reaped
                    termination["direct_wait_completed"] = True
                    if direct_reaped:
                        child_returncode = process.returncode

    elapsed = time.monotonic() - started
    result: dict[str, Any] = {
        "schema": "triad-bounded-run-v1",
        "outcome": outcome,
        "exit_code": int(exit_code),
        "child_exit_code": child_returncode,
        "timed_out": bool(timed_out),
        "cancelled": received_signal is not None,
        "cancel_signal": received_signal,
        "elapsed_seconds": elapsed,
        "timeout_seconds": args.timeout,
        "declared_max_seconds": args.max_seconds,
        "term_grace_seconds": args.term_grace,
        "maximum_cleanup_overhead_seconds": (2 * max(1.0, args.term_grace)
                                              + min(1.0, args.term_grace)),
        "log": str(log),
        "log_created": log_created,
        "termination": termination,
    }
    if setup_error is not None:
        result["setup_error"] = setup_error
    if termination is not None and (not termination.get("group_empty", False)
                                    or not termination.get("direct_child_reaped", True)):
        result["cleanup_error"] = "owned process group or direct child remained after bounded TERM/KILL cleanup"
        if process is not None:
            result["direct_pid"] = process.pid
        result["exit_code"] = EXIT_SETUP
        result["outcome"] = "cleanup_failure"

    try:
        _write_status(status, result)
    finally:
        for signum, handler in old_handlers.items():
            signal.signal(signum, handler)
    return int(result["exit_code"])


def main() -> int:
    parser = _build_parser()
    args = parser.parse_args()
    try:
        return run(args)
    except (RunnerError, OSError, ValueError) as exc:
        print(f"triad_bounded_run: {exc}", file=sys.stderr)
        return EXIT_SETUP


if __name__ == "__main__":
    raise SystemExit(main())

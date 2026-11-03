#!/usr/bin/env python3
"""Optional bounded NVIDIA telemetry for the isolated triad performance suite.

This invokes only the read-only selective query documented at
https://docs.nvidia.com/deploy/nvidia-smi/index.html . Device utilization is
sampled device-busy time, not SM occupancy, tensor occupancy, or a per-process
counter. NVIDIA's internal averaging period can overlap the requested window.
"""
from __future__ import annotations

from collections import deque
from contextlib import contextmanager
import csv
import math
import os
import re
import shutil
import signal
import subprocess
import threading
import time


FIELDS = ("gpu_busy_percent", "memory_busy_percent", "sm_clock_mhz",
          "power_watts", "temperature_celsius")
QUERY = ("utilization.gpu,utilization.memory,clocks.current.sm,power.draw,"
         "temperature.gpu,driver_version")
SAMPLE_INTERVAL_MS = 200
MAX_SAMPLES = 10_000
MAX_LINE_BYTES = 4096


def _parse_line(line: str):
    """Parse only the requested six CSV fields; never retain unexpected text."""
    try:
        values = next(csv.reader([line]))
    except (csv.Error, StopIteration):
        return None
    if len(values) != 6:
        return None
    result = {}
    for index, name in enumerate(FIELDS):
        item = values[index].strip()
        try:
            value = float(item)
        except ValueError:
            if item.upper() in ("N/A", "[N/A]", "NOT SUPPORTED", "[NOT SUPPORTED]"):
                result[name] = None
                continue
            return None
        if not math.isfinite(value):
            return None
        if index < 2 and not 0 <= value <= 100:
            return None
        if index in (2, 3) and value < 0:
            return None
        if index == 4 and not -273.15 <= value <= 1000:
            return None
        result[name] = value
    driver = values[-1].strip()
    # Accept version labels, never arbitrary diagnostic or identifier text.
    if re.fullmatch(r"\d+(?:\.\d+){1,4}", driver):
        result["driver_version"] = driver
    elif driver.upper() in ("N/A", "[N/A]"):
        result["driver_version"] = None
    else:
        return None
    return result


class _Sampler:
    def __init__(self, device_index, *, inherit_process_group=False, device_uuid=None):
        if isinstance(device_index, bool) or not isinstance(device_index, int) or not 0 <= device_index <= 255:
            raise ValueError("device_index must be an integer from 0 through 255")
        if not isinstance(inherit_process_group, bool):
            raise ValueError("inherit_process_group must be boolean")
        if device_uuid is not None and not re.fullmatch(r"GPU-[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}", device_uuid):
            raise ValueError("invalid selected CUDA device UUID")
        # Used only for local device selection. Never emit a hardware UUID.
        self._selector = device_uuid if device_uuid is not None else str(device_index)
        self._uuid_bound = device_uuid is not None
        self.device_index = device_index
        self._own_process_group = os.name == "posix" and not inherit_process_group
        self._samples = deque(maxlen=MAX_SAMPLES)
        self._lock = threading.Lock()
        self._process = None
        self._threads = []
        self._reason = None
        self._dropped = 0
        self._invalid_lines = 0
        self._stderr_bytes = 0
        self._cleanup = "not_started"
        self._closed = False

    def start(self):
        executable = shutil.which("nvidia-smi")
        if executable is None:
            self._reason = "nvidia-smi executable unavailable"
            return self
        command = [executable, f"--id={self._selector}", f"--query-gpu={QUERY}",
                   "--format=csv,noheader,nounits", f"--loop-ms={SAMPLE_INTERVAL_MS}"]
        try:
            self._process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0,
                start_new_session=self._own_process_group)
        except OSError as error:
            self._reason = f"nvidia-smi launch unavailable ({type(error).__name__})"
            return self
        self._cleanup = "running"
        try:
            for target in (self._read_stdout, self._drain_stderr):
                thread = threading.Thread(target=target, daemon=True, name="triad-gpu-telemetry")
                thread.start()
                self._threads.append(thread)
        except RuntimeError:
            self._reason = "telemetry reader startup unavailable"
            self.close()
        return self

    def _read_stdout(self):
        stream = self._process.stdout
        partial = bytearray()
        discard_line = False
        try:
            while True:
                chunk = stream.read(1024)
                if not chunk:
                    break
                # Raw pipe chunks are bounded independently of newline frequency.
                for byte in chunk:
                    if byte == 10:
                        if discard_line:
                            with self._lock:
                                self._invalid_lines += 1
                        else:
                            row = _parse_line(partial.decode("utf-8", errors="replace"))
                            with self._lock:
                                if row is None:
                                    self._invalid_lines += 1
                                else:
                                    if len(self._samples) == MAX_SAMPLES:
                                        self._dropped += 1
                                    self._samples.append((time.monotonic(), row))
                        partial.clear()
                        discard_line = False
                    elif not discard_line:
                        if len(partial) >= MAX_LINE_BYTES:
                            partial.clear()
                            discard_line = True
                        else:
                            partial.append(byte)
        except (OSError, ValueError):
            # Closing a failed/unavailable sampler cannot break receiver work.
            with self._lock:
                self._reason = "telemetry stdout reader unavailable"

    def _drain_stderr(self):
        try:
            while True:
                chunk = self._process.stderr.read(1024)
                if not chunk:
                    return
                with self._lock:
                    # Count only: driver diagnostics can contain identifiers.
                    self._stderr_bytes = min(2**63 - 1, self._stderr_bytes + len(chunk))
        except (OSError, ValueError):
            return

    def summarize(self, start_monotonic, end_monotonic):
        start, end = float(start_monotonic), float(end_monotonic)
        if not math.isfinite(start) or not math.isfinite(end) or end < start:
            raise ValueError("telemetry window must have finite ordered monotonic endpoints")
        with self._lock:
            rows = [(stamp, dict(row)) for stamp, row in self._samples if start <= stamp <= end]
            reason, dropped = self._reason, self._dropped
            invalid, stderr_bytes = self._invalid_lines, self._stderr_bytes
        process_code = self._process.poll() if self._process is not None else None
        summary = {}
        for field in FIELDS:
            numbers = [row[field] for _, row in rows if row[field] is not None]
            summary[field] = ({"status": "available", "sample_count": len(numbers),
                "min": min(numbers), "max": max(numbers), "mean": math.fsum(numbers) / len(numbers)}
                if numbers else {"status": "unavailable", "sample_count": 0})
        available = any(item["sample_count"] for item in summary.values())
        if not available and reason is None:
            reason = ("nvidia-smi query exited without usable counters" if process_code is not None
                      else "no usable telemetry sample completed inside this window")
        return {"status": "available" if available else "unavailable",
            "reason": reason, "device_index": self.device_index,
            "device_binding": "selected CUDA device UUID (identifier omitted)" if self._uuid_bound else "explicit physical nvidia-smi index",
            "source": "nvidia-smi selective read-only query", "sampling_interval_ms": SAMPLE_INTERVAL_MS,
            "window_seconds": end - start, "sample_count": len(rows), "metrics": summary,
            "driver_versions": sorted({row["driver_version"] for _, row in rows if row["driver_version"]}),
            "samples": [{"seconds_from_window_start": stamp - start, **row} for stamp, row in rows],
            "ring_capacity_samples": MAX_SAMPLES, "dropped_older_samples": dropped,
            "discarded_invalid_lines": invalid, "stderr_bytes_discarded": stderr_bytes,
            "process_returncode": process_code, "cleanup": self._cleanup,
            "confidence_interval": None,
            "interpretation": "device-busy percentages, not SM occupancy, tensor utilization, or per-process utilization; driver averaging windows can overlap requested endpoints; sample timestamps are host line-completion times",
            "coverage": "unavailable fields stay unavailable; short windows may contain no samples; dropped samples invalidate complete-window coverage"}

    def close(self):
        if self._closed:
            return
        self._closed = True
        process = self._process
        if process is None:
            self._cleanup = "not_started"
            return
        try:
            if self._own_process_group:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
            elif process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=1.)
            except subprocess.TimeoutExpired:
                pass
            # The process group can still contain descendants after leader exit.
            if self._own_process_group:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            elif process.poll() is None:
                process.kill()
            process.wait(timeout=1.)
            self._cleanup = "reaped"
        except (OSError, subprocess.TimeoutExpired):
            self._cleanup = "unavailable: process termination not verified"
        for thread in self._threads:
            thread.join(timeout=.25)
        # Avoid blocking on an IO lock if an abnormal reader survived teardown.
        if not any(thread.is_alive() for thread in self._threads):
            process.stdout.close()
            process.stderr.close()
        else:
            self._cleanup = "unavailable: telemetry reader termination not verified"


@contextmanager
def start_sampler(device_index=0, *, inherit_process_group=False, device_uuid=None):
    """Yield optional telemetry; always close/reap the read-only sampler.

    Call ``summarize`` with ``time.monotonic()`` endpoints for each warmed
    measurement window. Do not interpret samples as independent replications.
    Set ``inherit_process_group=True`` inside an enclosing process-group watchdog
    so its hard deadline also covers the sampler. Local cleanup then signals only
    this child, never the inherited runner group.
    """
    sampler = _Sampler(device_index, inherit_process_group=inherit_process_group, device_uuid=device_uuid).start()
    try:
        yield sampler
    finally:
        sampler.close()

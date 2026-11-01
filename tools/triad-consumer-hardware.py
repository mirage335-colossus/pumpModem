#!/usr/bin/env python3
"""Record sanitized CUDA-device and host-transfer measurements for Triad studies.

The probe measures warmed, persistent-buffer host/device copies only. It is not
a modem benchmark and does not report utilization or infer receiver throughput.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time
from typing import Any

try:
    import numpy as np
except Exception as error:  # pragma: no cover - environment dependent
    np = None
    _NUMPY_ERROR = f"{type(error).__name__}: {error}"
else:
    _NUMPY_ERROR = None

try:
    import torch
except Exception as error:  # pragma: no cover - environment dependent
    torch = None
    _TORCH_ERROR = f"{type(error).__name__}: {error}"
else:
    _TORCH_ERROR = None


SEED = 4_729_019
REPEATS = 7
WARMUPS = 1
SIZES_MIB = (8, 64)
SMI_FIELDS = (
    "pcie.link.gen.current", "pcie.link.gen.max",
    "pcie.link.width.current", "pcie.link.width.max",
    "power.draw", "power.limit", "clocks.gr", "clocks.mem",
)


def read_text(path: Path) -> str | None:
    try:
        return path.read_text().strip()
    except (OSError, UnicodeError):
        return None


def cpuinfo_sanitized() -> dict[str, Any]:
    """Expose model/capability facts, never per-CPU IDs or serial-like fields."""
    values: dict[str, str] = {}
    raw = read_text(Path("/proc/cpuinfo"))
    if raw:
        for line in raw.splitlines():
            if ":" not in line:
                continue
            key, value = (item.strip() for item in line.split(":", 1))
            normalized = key.lower()
            if normalized in {"vendor_id", "model name", "cpu family", "model",
                              "stepping", "cpu cores", "siblings", "flags"}:
                values.setdefault(normalized, value)
    return values


def quota_value(text: str | None) -> float | None:
    if not text:
        return None
    parts = text.split()
    try:
        if len(parts) == 2 and parts[0] != "max":  # cgroup v2 cpu.max
            quota, period = map(int, parts)
        elif len(parts) == 1:  # cgroup v1 quota file
            quota = int(parts[0])
            period_text = read_text(Path("/sys/fs/cgroup/cpu/cpu.cfs_period_us"))
            if period_text is None:
                period_text = read_text(Path("/sys/fs/cgroup/cpu,cpuacct/cpu.cfs_period_us"))
            if quota < 0 or period_text is None:
                return None
            period = int(period_text)
        else:
            return None
        return quota / period if quota > 0 and period > 0 else None
    except (ValueError, OverflowError):
        return None


def cpu_limits() -> dict[str, Any]:
    affinity_count = None
    try:
        affinity_count = len(os.sched_getaffinity(0))
    except (AttributeError, OSError):
        pass
    logical = os.cpu_count()
    quota = None
    source = None
    unlimited_source = None
    cgroup_v2 = read_text(Path("/sys/fs/cgroup/cpu.max"))
    if cgroup_v2 and cgroup_v2.split()[:1] == ["max"]:
        unlimited_source = "cgroup-v2"
    if unlimited_source is None:
        for path in (Path("/sys/fs/cgroup/cpu/cpu.cfs_quota_us"),
                     Path("/sys/fs/cgroup/cpu,cpuacct/cpu.cfs_quota_us")):
            quota_text = read_text(path)
            if quota_text == "-1":
                unlimited_source = "cgroup-v1"
                break
    for path, label in (
        (Path("/sys/fs/cgroup/cpu.max"), "cgroup-v2"),
        (Path("/sys/fs/cgroup/cpu/cpu.cfs_quota_us"), "cgroup-v1"),
        (Path("/sys/fs/cgroup/cpu,cpuacct/cpu.cfs_quota_us"), "cgroup-v1"),
    ):
        found = quota_value(read_text(path))
        if found is not None:
            quota, source = found, label
            break
    if source is None:
        source = unlimited_source
    usable = [x for x in (float(logical) if logical else None,
                          float(affinity_count) if affinity_count else None,
                          quota) if x is not None and x > 0]
    return {"logical_cpu_count": logical, "process_affinity_cpu_count": affinity_count,
            "cgroup_cpu_quota_cores": quota, "cgroup_version": source,
            "usable_cpu_quota_cores_estimate": min(usable) if usable else None}


def device_inventory(device: str) -> dict[str, Any]:
    info: dict[str, Any] = {"requested_device": device}
    if torch is None:
        info.update(status="unavailable", reason="PyTorch import failed", error=_TORCH_ERROR)
        return info
    info.update(torch_version=str(torch.__version__), cuda_build=torch.version.cuda,
                cuda_available=bool(torch.cuda.is_available()))
    if not torch.cuda.is_available():
        info.update(status="unavailable", reason="CUDA is not available in this PyTorch runtime")
        return info
    try:
        parsed = torch.device(device)
        if parsed.type != "cuda":
            raise ValueError("device must be a CUDA device")
        index = torch.cuda.current_device() if parsed.index is None else parsed.index
        prop = torch.cuda.get_device_properties(index)
        info.update(status="available", device_index=index,
                    name=str(prop.name), compute_capability=f"{prop.major}.{prop.minor}",
                    total_memory_bytes=int(prop.total_memory),
                    multiprocessor_count=int(prop.multi_processor_count),
                    registers_per_block=getattr(prop, "regs_per_block", None),
                    shared_memory_per_block_bytes=getattr(prop, "shared_memory_per_block", None),
                    max_threads_per_multiprocessor=getattr(prop, "max_threads_per_multi_processor", None))
    except Exception as error:
        info.update(status="unavailable", error=f"{type(error).__name__}: {error}")
    return info


def nvidia_smi_inventory(device_index: int | None) -> dict[str, Any]:
    """Query only physical link/power/clock fields; never query UUID/host IDs."""
    executable = shutil.which("nvidia-smi")
    if not executable:
        return {"status": "unavailable", "reason": "nvidia-smi executable not found",
                "queried_fields": list(SMI_FIELDS)}
    if device_index is None:
        return {"status": "unavailable", "reason": "CUDA device index unavailable",
                "queried_fields": list(SMI_FIELDS)}
    command = [executable, "-i", str(device_index),
               "--query-gpu=" + ",".join(SMI_FIELDS),
               "--format=csv,noheader,nounits"]
    try:
        result = subprocess.run(command, capture_output=True, text=True, check=False, timeout=10)
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"status": "unavailable", "reason": f"nvidia-smi query could not complete ({type(error).__name__})",
                "queried_fields": list(SMI_FIELDS)}
    if result.returncode != 0:
        # Do not persist raw stderr because vendor tools may include machine identifiers.
        return {"status": "unavailable", "returncode": result.returncode,
                "reason": "nvidia-smi query failed; raw stderr intentionally omitted",
                "queried_fields": list(SMI_FIELDS)}
    rows = [line for line in result.stdout.splitlines() if line.strip()]
    if len(rows) != 1:
        return {"status": "unavailable", "reason": "expected one selected-device result row",
                "queried_fields": list(SMI_FIELDS)}
    fields = [part.strip() for part in rows[0].split(",")]
    if len(fields) != len(SMI_FIELDS):
        return {"status": "unavailable", "reason": "unexpected nvidia-smi field count",
                "queried_fields": list(SMI_FIELDS)}
    return {"status": "available", "queried_fields": list(SMI_FIELDS),
            "values": dict(zip(SMI_FIELDS, fields)),
            "command_fields_only": True}


def sync(device: Any) -> None:
    torch.cuda.synchronize(device)


def event_seconds(first: Any, last: Any) -> float:
    return float(first.elapsed_time(last)) / 1000.0


def summarize(samples: list[float], byte_count: int) -> dict[str, Any]:
    ordered = sorted(samples)
    mean = sum(samples) / len(samples)
    return {"samples_seconds": samples, "mean_seconds": mean,
            "median_seconds": ordered[len(ordered)//2],
            "min_seconds": min(samples), "max_seconds": max(samples),
            "mean_GiB_per_second": (byte_count / (1024**3)) / mean if mean else None}


def timed_transfer(label: str, direction: str, source: Any, destination: Any,
                   device: Any, *, non_blocking: bool, byte_count: int) -> dict[str, Any]:
    """Time copy into preallocated buffers with CUDA events and synchronized wall time."""
    def copy() -> None:
        destination.copy_(source, non_blocking=non_blocking)

    copy()
    sync(device)
    wall: list[float] = []
    event: list[float] = []
    for _ in range(REPEATS):
        start_event = torch.cuda.Event(enable_timing=True)
        end_event = torch.cuda.Event(enable_timing=True)
        sync(device)
        started = time.perf_counter()
        start_event.record()
        copy()
        end_event.record()
        end_event.synchronize()
        wall.append(time.perf_counter() - started)
        event.append(event_seconds(start_event, end_event))
    return {"label": label, "direction": direction,
            "host_memory_kind": "pinned" if "pinned" in label else "pageable",
            "non_blocking_requested": non_blocking, "bytes": byte_count,
            "repeats": REPEATS, "warmup_copies": WARMUPS,
            "wall": summarize(wall, byte_count),
            "cuda_event": summarize(event, byte_count),
            "measurement_scope": "preallocated persistent buffers; includes synchronization in wall time"}


def run_transfer_measurements(device: Any) -> dict[str, Any]:
    torch.cuda.set_device(device)
    rng = torch.Generator(device="cpu").manual_seed(SEED)
    results: list[dict[str, Any]] = []
    for mib in SIZES_MIB:
        count = mib * 1024 * 1024
        host_alloc: dict[str, float] = {}
        start = time.perf_counter()
        pageable = torch.empty(count, dtype=torch.uint8, device="cpu", pin_memory=False)
        host_alloc["pageable_host_allocation_seconds"] = time.perf_counter() - start
        start = time.perf_counter()
        pinned_h2d = torch.empty(count, dtype=torch.uint8, device="cpu", pin_memory=True)
        host_alloc["pinned_h2d_host_allocation_seconds"] = time.perf_counter() - start
        start = time.perf_counter()
        pinned_d2h = torch.empty(count, dtype=torch.uint8, device="cpu", pin_memory=True)
        host_alloc["pinned_d2h_host_allocation_seconds"] = time.perf_counter() - start
        start = time.perf_counter()
        device_buffer = torch.empty(count, dtype=torch.uint8, device=device)
        sync(device)
        host_alloc["persistent_device_allocation_and_sync_seconds"] = time.perf_counter() - start
        start = time.perf_counter()
        pageable.copy_(torch.randint(0, 256, (count,), generator=rng, dtype=torch.uint8))
        sync(device)
        host_alloc["pageable_host_fill_seconds"] = time.perf_counter() - start
        # Same deterministic bytes feed each path. Host copies are completed
        # outside timed intervals; fill cost is reported separately above.
        pinned_h2d.copy_(pageable)
        sync(device)
        start = time.perf_counter()
        device_source = torch.empty(count, dtype=torch.uint8, device=device)
        sync(device)
        host_alloc["persistent_d2h_source_allocation_and_sync_seconds"] = time.perf_counter() - start
        device_source.copy_(pinned_h2d, non_blocking=True)
        sync(device)
        results.append({"size_mib": mib, "bytes": count,
                        "allocation_and_preparation_seconds": host_alloc,
                        "h2d": [
                            timed_transfer("pageable_h2d", "host_to_device", pageable,
                                           device_buffer, device, non_blocking=False,
                                           byte_count=count),
                            timed_transfer("pinned_h2d", "host_to_device", pinned_h2d,
                                           device_buffer, device, non_blocking=True,
                                           byte_count=count)],
                        "d2h": [
                            timed_transfer("pageable_d2h", "device_to_host", device_source,
                                           pageable, device, non_blocking=False,
                                           byte_count=count),
                            timed_transfer("pinned_d2h", "device_to_host", device_source,
                                           pinned_d2h, device, non_blocking=True,
                                           byte_count=count)]})
        del pageable, pinned_h2d, pinned_d2h, device_buffer, device_source
        sync(device)
    return {"status": "complete", "seed": SEED, "sizes_mib": list(SIZES_MIB),
            "repeats": REPEATS, "warmups": WARMUPS,
            "results": results,
            "interpretation": "transfer-only with persistent allocations; event time excludes CPU staging, synchronized wall time includes call overhead; no utilization or modem-throughput claim"}


def collect(args: argparse.Namespace) -> dict[str, Any]:
    inventory = device_inventory(args.device)
    index = inventory.get("device_index") if inventory.get("status") == "available" else None
    info = {"schema": "triad-consumer-hardware-probe-v1",
            "status": "complete", "requested_device": args.device,
            "versions": {"python": platform.python_version(),
                         "numpy": str(np.__version__) if np is not None else None,
                         "numpy_import_error": _NUMPY_ERROR,
                         "torch": str(torch.__version__) if torch is not None else None,
                         "torch_import_error": _TORCH_ERROR,
                         "torch_cuda_build": torch.version.cuda if torch is not None else None},
            "host": {"platform_system": platform.system(),
                     "kernel_release": platform.release(),
                     "machine_architecture": platform.machine(),
                     "cpu": cpuinfo_sanitized(),
                     "cpu_limits": cpu_limits()},
            "cuda_device": inventory,
            "physical_gpu_telemetry": nvidia_smi_inventory(index),
            "measurement_contract": {"fixed_seed": SEED, "repeats": REPEATS,
                "warmups": WARMUPS, "sizes_mib": list(SIZES_MIB),
                "pageable_and_pinned_host_buffers": True,
                "device_buffers_persistent_during_repeats": True,
                "allocation_and_pinning_measured_separately": True,
                "utilization_or_peak_throughput_claimed": False}}
    if inventory.get("status") != "available":
        info["transfers"] = {"status": "not_run", "reason": inventory.get("reason", "CUDA unavailable")}
        info["status"] = "inventory_only"
        return info
    try:
        device = torch.device(args.device)
        info["transfers"] = run_transfer_measurements(device)
    except Exception as error:
        info["transfers"] = {"status": "failed", "error": f"{type(error).__name__}: {error}"}
        info["status"] = "measurement_failed"
    return info


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", default="cuda:0")
    parser.add_argument("--output", type=Path, required=True,
                        help="new JSON path; existing files are never overwritten")
    args = parser.parse_args()
    if args.output.exists():
        parser.error(f"output already exists: {args.output}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    result = collect(args)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"status": result["status"], "output": str(args.output),
                      "cuda_status": result["cuda_device"].get("status"),
                      "transfer_status": result["transfers"].get("status")}, sort_keys=True))
    return 0 if result["status"] in ("complete", "inventory_only") else 1


if __name__ == "__main__":
    raise SystemExit(main())

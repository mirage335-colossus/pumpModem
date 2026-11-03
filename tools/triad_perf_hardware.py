#!/usr/bin/env python3
"""Short, source-independent CPU/CUDA calibration for the isolated Triad suite.

This measures hardware kernels and records their actual arithmetic paths. It
does not choose a receiver precision or predict receiver time; those decisions
need the receiver's numerical comparison and stage model. In particular, a
PyTorch tensor dtype is never taken as evidence that tensor instructions ran.
"""
from __future__ import annotations

import argparse
import contextlib
import json
import math
import os
from pathlib import Path
import platform
import re
import sys
import time
from typing import Any, Callable

try:
    import numpy as np
    from scipy import fft as scipy_fft
except Exception as error:  # pragma: no cover - installation dependent
    np = None
    scipy_fft = None
    _NUMPY_IMPORT_ERROR = f"{type(error).__name__}: {error}"
else:
    _NUMPY_IMPORT_ERROR = None

try:
    import torch
except Exception as error:  # pragma: no cover - installation dependent
    torch = None
    _TORCH_IMPORT_ERROR = f"{type(error).__name__}: {error}"
else:
    _TORCH_IMPORT_ERROR = None

SCHEMA = "triad-performance-hardware-profile-v1"
MAX_SECONDS_PER_CASE = 20.0
MIN_SECONDS_PER_CASE = 0.01
MAX_WORKSPACE_BYTES = 8 * 1024**3
DEFAULT_WORKSPACE_BYTES = 512 * 1024**2
CPU_CASES = (
    "gemm-fp32", "gemm-fp64", "fft-complex64", "fft-complex128",
    "stream-copy", "phase-reduction-complex64", "phase-reduction-complex128",
    "batched-svd-fp64",
)
CPU_TENSOR_DTYPES = ("fp16", "bf16")
GPU_CASES = ("gemm-fp32-strict", "gemm-fp64", "fft-complex64", "fft-complex128", "stream-copy",
             "phase-reduction-complex64", "batched-svd-fp32")
GPU_TENSOR_FORMATS = ("tf32", "fp16", "bf16", "fp8-e4m3", "fp8-e5m2", "int8")


def _read(path: str) -> str | None:
    try:
        return Path(path).read_text().strip()
    except (OSError, UnicodeError):
        return None


def _cpu_features() -> dict[str, Any]:
    """Return non-unique CPU identity/capability fields; omit serials/hostnames."""
    allowed = {"vendor_id", "model name", "cpu family", "model", "stepping",
               "cpu cores", "siblings", "flags", "features"}
    data: dict[str, str] = {}
    raw = _read("/proc/cpuinfo")
    if raw:
        for line in raw.splitlines():
            if ":" not in line:
                continue
            key, value = (part.strip() for part in line.split(":", 1))
            if key.lower() in allowed:
                data.setdefault(key.lower(), value)
    return data


def _cpu_limits() -> dict[str, Any]:
    logical = os.cpu_count()
    affinity = None
    try:
        affinity = len(os.sched_getaffinity(0))
    except (AttributeError, OSError):
        pass

    quota = None
    source = None
    v2 = _read("/sys/fs/cgroup/cpu.max")
    candidates: list[tuple[str, str | None]] = [("cgroup-v2", v2)]
    for path in ("/sys/fs/cgroup/cpu/cpu.cfs_quota_us",
                 "/sys/fs/cgroup/cpu,cpuacct/cpu.cfs_quota_us"):
        candidates.append(("cgroup-v1", _read(path)))
    for version, text in candidates:
        if not text:
            continue
        parts = text.split()
        try:
            if version == "cgroup-v2" and len(parts) == 2 and parts[0] != "max":
                q, period = int(parts[0]), int(parts[1])
            elif version == "cgroup-v1" and len(parts) == 1 and int(parts[0]) > 0:
                q = int(parts[0])
                period_text = (_read("/sys/fs/cgroup/cpu/cpu.cfs_period_us") or
                               _read("/sys/fs/cgroup/cpu,cpuacct/cpu.cfs_period_us"))
                if period_text is None:
                    continue
                period = int(period_text)
            else:
                continue
            if q > 0 and period > 0:
                quota, source = q / period, version
                break
        except (ValueError, OverflowError):
            continue
    positive = [float(value) for value in (logical, affinity, quota)
                if value is not None and float(value) > 0]
    return {"logical_cpu_count": logical, "affinity_cpu_count": affinity,
            "quota_cores": quota, "quota_source": source,
            "effective_core_limit_estimate": min(positive) if positive else None}


def discover_hardware(device: str = "cpu", threads: int = 1) -> dict[str, Any]:
    """Return JSON-ready sanitized platform/dependency and device capability facts."""
    if not isinstance(device, str) or not device:
        raise ValueError("device must be cpu or cuda[:index]")
    if isinstance(threads, bool) or not isinstance(threads, int) or threads < 1:
        raise ValueError("threads must be a positive integer")
    if device != "cpu" and not device.startswith("cuda"):
        raise ValueError("device must be cpu or cuda[:index]")
    result: dict[str, Any] = {
        "requested_device": device,
        "os": {"system": platform.system(), "release": platform.release(),
               "machine": platform.machine(), "python": platform.python_version()},
        "cpu": {**_cpu_features(), **_cpu_limits()},
        "requested_threads": threads,
        "dispatch_environment": {name: os.environ.get(name) for name in (
            "ATEN_CPU_CAPABILITY", "OPENBLAS_CORETYPE", "MKL_ENABLE_INSTRUCTIONS",
            "ONEDNN_MAX_CPU_ISA", "NPY_DISABLE_CPU_FEATURES")},
        "dependencies": {
            "numpy": getattr(np, "__version__", None) if np is not None else None,
            "scipy": getattr(__import__("scipy"), "__version__", None) if scipy_fft is not None else None,
            "torch": str(torch.__version__) if torch is not None else None,
            "numpy_import_error": _NUMPY_IMPORT_ERROR,
            "torch_import_error": _TORCH_IMPORT_ERROR,
        },
    }
    if np is not None:
        try:
            simd = np.__config__.CONFIG.get("SIMD Extensions", {})
            result["cpu"]["numpy_simd"] = simd
            builds = np.__config__.CONFIG.get("Build Dependencies", {})
            blas = builds.get("blas", {})
            lapack = builds.get("lapack", {})
            result["cpu"]["numpy_blas"] = {
                "name": blas.get("name"), "version": blas.get("version"),
                "configuration": blas.get("openblas configuration"),
            }
            result["cpu"]["numpy_lapack"] = {
                "name": lapack.get("name"), "version": lapack.get("version"),
                "configuration": lapack.get("openblas configuration"),
            }
        except (AttributeError, TypeError):
            result["cpu"]["numpy_simd"] = None
            result["cpu"]["numpy_blas"] = None
            result["cpu"]["numpy_lapack"] = None
    if torch is not None:
        try:
            result["cpu"]["torch_capability"] = torch.backends.cpu.get_cpu_capability()
        except (AttributeError, RuntimeError):
            result["cpu"]["torch_capability"] = None
    if device == "cpu":
        result["device"] = {"status": "available"}
        return result
    if torch is None:
        result["device"] = {"status": "unavailable", "reason": "PyTorch import failed"}
        return result
    if not torch.cuda.is_available():
        result["device"] = {"status": "unavailable", "reason": "CUDA unavailable in this PyTorch runtime",
                            "torch_cuda_build": torch.version.cuda}
        return result
    try:
        parsed = torch.device(device)
        if parsed.type != "cuda":
            raise ValueError("device must be cpu or cuda[:index]")
        index = torch.cuda.current_device() if parsed.index is None else parsed.index
        if not 0 <= index < torch.cuda.device_count():
            raise ValueError("CUDA device index is out of range")
        prop = torch.cuda.get_device_properties(index)
        driver_getter = getattr(torch._C, "_cuda_getDriverVersion", None)
        driver = driver_getter() if driver_getter else None
        result["device"] = {
            "status": "available", "index": index, "name": str(prop.name),
            "compute_capability": [int(prop.major), int(prop.minor)],
            "total_memory_bytes": int(prop.total_memory),
            "multiprocessor_count": int(prop.multi_processor_count),
            "torch_cuda_build": torch.version.cuda,
            "driver_version": driver, "driver_version_method": "PyTorch in-process API" if driver is not None else None,
            "hardware_utilization": {"status": "unavailable", "reason": "no in-process utilization counter used"},
        }
    except (RuntimeError, ValueError) as error:
        result["device"] = {"status": "unavailable", "error": f"{type(error).__name__}: {error}"}
    return result


def _validate(device: str, threads: int, seconds: float, memory_limit_bytes: int) -> None:
    if not isinstance(device, str) or not device or (device != "cpu" and not device.startswith("cuda")):
        raise ValueError("device must be cpu or cuda[:index]")
    if isinstance(threads, bool) or not isinstance(threads, int) or threads < 1:
        raise ValueError("threads must be a positive integer")
    if isinstance(seconds, bool) or not isinstance(seconds, (int, float)) or not math.isfinite(seconds):
        raise ValueError("seconds must be finite")
    if not MIN_SECONDS_PER_CASE <= seconds <= MAX_SECONDS_PER_CASE:
        raise ValueError(f"seconds per case must be in [{MIN_SECONDS_PER_CASE}, {MAX_SECONDS_PER_CASE}]")
    if (isinstance(memory_limit_bytes, bool) or not isinstance(memory_limit_bytes, int) or
            not 32 * 1024**2 <= memory_limit_bytes <= MAX_WORKSPACE_BYTES):
        raise ValueError("memory limit must be an integer between 32 MiB and 8 GiB")


def _estimate_case(workspace: int, work: dict[str, float], seconds: float,
                   wall_seconds: float, cpu_seconds: float, threads: int,
                   elapsed_cuda_seconds: float | None = None) -> dict[str, Any]:
    result = {"status": "complete", "configured_seconds": seconds,
              "wall_seconds": wall_seconds, "process_cpu_seconds": cpu_seconds,
              "process_cpu_cores_used": cpu_seconds / wall_seconds if wall_seconds > 0 else None,
              "requested_threads": threads, "largest_estimated_case_workspace_bytes": workspace,
              **work}
    if elapsed_cuda_seconds is not None:
        result["completed_cuda_seconds"] = elapsed_cuda_seconds
        result["cuda_to_wall_fraction"] = elapsed_cuda_seconds / wall_seconds if wall_seconds > 0 else None
    return result


def _time_cpu_case(name: str, fn: Callable[[], Any], work: Callable[[], dict[str, float]],
                   *, seconds: float, threads: int, workspace: int,
                   deadline: float | None) -> dict[str, Any]:
    if deadline is not None and time.monotonic() >= deadline:
        return {"status": "incomplete", "reason": "profile deadline reached before case"}
    wall_start, cpu_start = time.perf_counter(), time.process_time()
    count = 0
    try:
        # Keep the first dispatch/allocation/plan path visible. It is not
        # discarded as warm-up: callers need both this cost and steady-state
        # throughput when reasoning about short receiver jobs.
        cold_start = time.perf_counter()
        cold_output = fn()
        cold_seconds = time.perf_counter() - cold_start
        if cold_output is not None:
            if torch is not None and isinstance(cold_output, torch.Tensor):
                if not bool(torch.isfinite(cold_output).all()):
                    raise FloatingPointError("nonfinite benchmark result")
            elif np is not None and isinstance(cold_output, np.ndarray):
                if not np.isfinite(cold_output).all():
                    raise FloatingPointError("nonfinite benchmark result")
        del cold_output
        warm_start = time.perf_counter()
        while time.perf_counter() - warm_start < seconds:
            if deadline is not None and time.monotonic() >= deadline:
                return {"status": "incomplete", "reason": "profile deadline reached during case",
                        "completed_iterations": count,
                        "elapsed_wall_seconds": time.perf_counter() - wall_start}
            output = fn()
            if count == 0 and output is not None:
                # A cheap finite check catches broken arithmetic without timing
                # every result or turning this profile into a receiver oracle.
                if torch is not None and isinstance(output, torch.Tensor):
                    if not bool(torch.isfinite(output).all()):
                        raise FloatingPointError("nonfinite benchmark result")
                elif np is not None and isinstance(output, np.ndarray):
                    if not np.isfinite(output).all():
                        raise FloatingPointError("nonfinite benchmark result")
            count += 1
        if deadline is not None and time.monotonic() >= deadline:
            return {"status": "incomplete", "reason": "profile deadline reached at case boundary",
                    "completed_iterations": count,
                    "elapsed_wall_seconds": time.perf_counter() - wall_start}
        wall = time.perf_counter() - wall_start
        cpu = time.process_time() - cpu_start
        warm_wall = time.perf_counter() - warm_start
        metadata = work()
        timed = _estimate_case(workspace, metadata, seconds, wall, cpu, threads)
        timed["operations_per_second"] = (
            metadata["estimated_operations_per_iteration"] * count / max(warm_wall, 1e-12)
            if "estimated_operations_per_iteration" in metadata and count else None)
        timed["bytes_per_second"] = (
            metadata.get("bytes_moved_per_iteration", metadata.get("estimated_read_write_bytes_per_iteration"))
            * count / max(warm_wall, 1e-12)
            if metadata.get("bytes_moved_per_iteration", metadata.get("estimated_read_write_bytes_per_iteration")) is not None and count else None)
        return {**timed,
                "cold_first_call_seconds": cold_seconds,
                "warm_measurement_wall_seconds": warm_wall,
                "completed_warm_iterations": count,
                "timing_method": "host wall and process CPU clocks; cold first call separated"}
    except MemoryError:
        return {"status": "unsupported", "reason": "allocation exceeded the declared process budget",
                "completed_iterations": count}
    except (RuntimeError, ValueError, FloatingPointError, OverflowError) as error:
        return {"status": "failed", "reason": f"{type(error).__name__}: {error}",
                "completed_iterations": count}


def _cpu_cases(threads: int, seconds: float, memory_limit_bytes: int,
               deadline: float | None) -> dict[str, Any]:
    if np is None or scipy_fft is None or torch is None:
        return {name: {"status": "unavailable", "reason": "NumPy, SciPy and PyTorch are required",
                       "dependency_errors": {"numpy_scipy": _NUMPY_IMPORT_ERROR,
                                             "torch": _TORCH_IMPORT_ERROR}}
                for name in CPU_CASES + tuple(f"tensor-{x}" for x in CPU_TENSOR_DTYPES)}
    old_threads = torch.get_num_threads()
    cases: dict[str, Any] = {}
    try:
        torch.set_num_threads(threads)

        for name, dtype in (("gemm-fp32", torch.float32), ("gemm-fp64", torch.float64)):
            n = 512
            itemsize = torch.empty((), dtype=dtype).element_size()
            estimated = 3 * n * n * itemsize
            if estimated > memory_limit_bytes:
                cases[name] = {"status": "unsupported", "reason": "GEMM buffers exceed declared memory limit",
                               "estimated_workspace_bytes": estimated}
                continue
            a = torch.randn((n, n), dtype=dtype)
            b = torch.randn((n, n), dtype=dtype)
            cases[name] = _time_cpu_case(name, lambda: torch.mm(a, b),
                lambda: {"work_kind": "dense square GEMM", "shape": [n, n, n],
                         "estimated_operations_per_iteration": float(2*n*n*n),
                         "input_storage": str(dtype), "multiplication": str(dtype),
                         "accumulation": "backend-defined", "output": str(dtype),
                         "approximate_operand_and_output_bytes_per_iteration": float(estimated)},
                seconds=seconds, threads=threads, workspace=estimated, deadline=deadline)

        for name, dtype in (("fft-complex64", np.complex64), ("fft-complex128", np.complex128)):
            batch, n = 8, 32_768
            estimated = 2 * batch * n * np.dtype(dtype).itemsize
            if estimated > memory_limit_bytes:
                cases[name] = {"status": "unsupported", "reason": "FFT buffers exceed declared memory limit",
                               "estimated_workspace_bytes": int(estimated)}
                continue
            values = np.ones((batch, n), dtype=dtype)
            logn = math.log2(n)
            with scipy_fft.set_workers(threads):
                fn = lambda values=values: scipy_fft.fft(values, axis=-1, overwrite_x=False, workers=threads)
                cases[name] = _time_cpu_case(name, fn,
                    lambda: {"work_kind": "batched full-length complex FFT", "shape": [batch, n],
                             "estimated_operations_per_iteration": float(5*batch*n*logn),
                             "input_storage": np.dtype(dtype).name, "multiplication": "backend FFT",
                             "accumulation": "backend FFT", "output": np.dtype(dtype).name,
                             "estimated_read_write_bytes_per_iteration": int(estimated)},
                    seconds=seconds, threads=threads, workspace=int(estimated), deadline=deadline)

        stream_bytes = 32 * 1024**2
        stream_workspace = 2 * stream_bytes
        if stream_workspace > memory_limit_bytes:
            cases["stream-copy"] = {"status": "unsupported", "reason": "streaming buffers exceed declared memory limit",
                                     "estimated_workspace_bytes": stream_workspace}
        else:
            source = np.arange(stream_bytes // np.dtype(np.float32).itemsize, dtype=np.float32)
            destination = np.empty_like(source)
            cases["stream-copy"] = _time_cpu_case("stream-copy", lambda: np.copyto(destination, source),
                lambda: {"work_kind": "streaming memory copy", "shape": [int(source.size)],
                         "input_storage": "float32", "multiplication": "none", "accumulation": "none",
                         "output": "float32", "bytes_moved_per_iteration": 2*stream_bytes},
                seconds=seconds, threads=threads, workspace=stream_workspace, deadline=deadline)

        for name, dtype in (("phase-reduction-complex64", np.complex64),
                            ("phase-reduction-complex128", np.complex128)):
            n = 1_048_576
            itemsize = np.dtype(dtype).itemsize
            # Source, oscillator and output are simultaneously resident.
            estimated = 3*n*itemsize
            if estimated > memory_limit_bytes:
                cases[name] = {"status": "unsupported", "reason": "phase/reduction buffers exceed declared memory limit",
                               "estimated_workspace_bytes": int(estimated)}
                continue
            source = np.ones(n, dtype=dtype)
            oscillator = np.exp(1j*np.linspace(0., 2.*np.pi, n, endpoint=False, dtype=np.float64)).astype(dtype)
            output = np.empty_like(source)
            def phase_and_sum(source=source, oscillator=oscillator, output=output):
                np.multiply(source, oscillator, out=output)
                return np.vdot(output, output).real
            cases[name] = _time_cpu_case(name, phase_and_sum,
                lambda name=name, itemsize=itemsize, n=n: {
                    "work_kind": "samplewise complex phase product and whole-array energy reduction",
                    "shape": [n], "input_storage": "complex64" if name.endswith("64") else "complex128",
                    "multiplication": "complex floating point", "accumulation": "vdot reduction",
                    "output": "float64", "estimated_read_write_bytes_per_iteration": int(3*n*itemsize)},
                seconds=seconds, threads=threads, workspace=estimated, deadline=deadline)

        batch, rows, cols = 64, 24, 24
        estimated = batch*rows*cols*8
        if estimated > memory_limit_bytes:
            cases["batched-svd-fp64"] = {"status": "unsupported", "reason": "SVD buffers exceed declared memory limit",
                                         "estimated_workspace_bytes": estimated}
        else:
            matrices = np.ones((batch, rows, cols), dtype=np.float64)
            # A tiny asymmetry avoids a deliberately rank-one-only SVD path.
            matrices[:, np.arange(rows), np.arange(cols)] += 1.
            cases["batched-svd-fp64"] = _time_cpu_case("batched-svd-fp64",
                lambda: np.linalg.svd(matrices, full_matrices=False, compute_uv=False),
                lambda: {"work_kind": "batched small dense SVD singular values", "shape": [batch, rows, cols],
                         "input_storage": "float64", "multiplication": "backend LAPACK",
                         "accumulation": "backend LAPACK", "output": "float64 singular values",
                         "estimated_input_bytes_per_iteration": estimated},
                seconds=seconds, threads=threads, workspace=estimated, deadline=deadline)

        cases.update(_cpu_tensor_cases(threads, seconds, memory_limit_bytes, deadline))
    finally:
        torch.set_num_threads(old_threads)
    return cases


def _cpu_tensor_cases(threads: int, seconds: float, memory_limit_bytes: int,
                      deadline: float | None) -> dict[str, Any]:
    cases = {}
    n = 256
    for name, dtype in (("tensor-fp16-cpu", torch.float16), ("tensor-bf16-cpu", torch.bfloat16)):
        estimated = 3*n*n*torch.empty((), dtype=dtype).element_size()
        if estimated > memory_limit_bytes:
            cases[name] = {"status": "unsupported", "reason": "tensor GEMM buffers exceed declared memory limit"}
            continue
        try:
            a = torch.randn((n, n), dtype=dtype)
            b = torch.randn((n, n), dtype=dtype)
            cases[name] = _time_cpu_case(name, lambda a=a,b=b: torch.mm(a,b),
                lambda name=name,dtype=dtype,n=n,estimated=estimated: {
                    "work_kind": "PyTorch CPU low-precision GEMM stress only",
                    "shape": [n,n,n], "estimated_operations_per_iteration": float(2*n*n*n),
                    "input_storage": str(dtype), "multiplication": str(dtype),
                    "accumulation": "backend-defined", "output": str(dtype),
                    "execution_path": "PyTorch CPU backend; ISA-level implementation unverified",
                    "tensor_instruction_verification": "unavailable for portable CPU dispatch",
                    "approximate_operand_and_output_bytes_per_iteration": float(estimated)},
                seconds=seconds, threads=threads, workspace=estimated, deadline=deadline)
        except RuntimeError as error:
            cases[name] = {"status": "unsupported", "reason": f"CPU backend does not execute this dtype ({type(error).__name__})"}
    return cases


def _cuda_event_case(name: str, fn: Callable[[], Any], work: Callable[[], dict[str, Any]],
                     *, device: Any, seconds: float, threads: int,
                     workspace: int, deadline: float | None) -> dict[str, Any]:
    if deadline is not None and time.monotonic() >= deadline:
        return {"status": "incomplete", "reason": "profile deadline reached before case"}
    started_wall, started_cpu = time.perf_counter(), time.process_time()
    try:
        # Keep first-use CUDA context/JIT/allocator cost separate from steady
        # device execution; retain it in wall time and the run-level budget.
        cold_start = time.perf_counter()
        cold_output = fn()
        torch.cuda.synchronize(device)
        cold_seconds = time.perf_counter() - cold_start
        del cold_output
        # Synchronize bounded batches, never enqueue for five host seconds.
        # Adapt toward <=20 ms completed device work per batch; a single slow
        # dispatch remains indivisible and is bounded by the runner watchdog.
        warm_start = time.perf_counter()
        warm_monotonic = time.monotonic()
        count, batch, cuda = 0, 1, 0.0
        while time.perf_counter() - warm_start < seconds:
            if deadline is not None and time.monotonic() >= deadline:
                return {"status": "incomplete", "reason": "profile deadline reached during case",
                        "completed_iterations": count,
                        "elapsed_wall_seconds": time.perf_counter() - started_wall,
                        "completed_cuda_seconds": cuda, "cold_first_call_seconds": cold_seconds}
            begin, end = torch.cuda.Event(enable_timing=True), torch.cuda.Event(enable_timing=True)
            begin.record()
            for _ in range(batch):
                output = fn()
            end.record()
            end.synchronize()
            elapsed = float(begin.elapsed_time(end)) / 1000.0
            cuda += elapsed
            if count == 0 and isinstance(output, torch.Tensor) and not bool(torch.isfinite(output).all()):
                raise FloatingPointError("nonfinite CUDA benchmark result")
            count += batch
            remaining = max(0., seconds - (time.perf_counter() - warm_start))
            if deadline is not None:
                remaining = min(remaining, max(0., deadline - time.monotonic()))
            batch = max(1, min(256, int(min(.02, remaining) * batch / max(elapsed, 1e-6))))
        wall = time.perf_counter() - started_wall
        warm_wall = time.perf_counter() - warm_start
        cpu = time.process_time() - started_cpu
        if deadline is not None and time.monotonic() >= deadline:
            return {"status": "incomplete", "reason": "profile deadline reached at synchronized case boundary",
                    "completed_iterations": count, "elapsed_wall_seconds": wall,
                    "completed_cuda_seconds": cuda, "cold_first_call_seconds": cold_seconds}
        metadata = work()
        timed = _estimate_case(workspace, metadata, seconds, wall, cpu, threads, cuda)
        timed["operations_per_second"] = (
            metadata["estimated_operations_per_iteration"] * count / max(cuda, 1e-12)
            if "estimated_operations_per_iteration" in metadata and cuda > 0 else None)
        timed["bytes_per_second"] = (
            metadata.get("bytes_moved_per_iteration", metadata.get("estimated_read_write_bytes_per_iteration"))
            * count / max(cuda, 1e-12)
            if metadata.get("bytes_moved_per_iteration", metadata.get("estimated_read_write_bytes_per_iteration")) is not None and cuda > 0 else None)
        return {**timed,
                "cold_first_call_seconds": cold_seconds, "completed_iterations": count,
                "warm_measurement_wall_seconds": warm_wall,
                "telemetry_window_monotonic": [warm_monotonic, time.monotonic()],
                "timing_method": "completed CUDA events in target-20ms adaptive synchronized batches plus host wall/process clocks",
                "cuda_event_span_fraction_of_warm_wall": cuda / max(warm_wall, 1e-12),
                "utilization_scope": "CUDA event spans can contain submission gaps; not measured SM or tensor utilization"}
    except MemoryError:
        return {"status": "unsupported", "reason": "allocation exceeded the declared memory budget"}
    except (RuntimeError, ValueError, FloatingPointError) as error:
        return {"status": "failed", "reason": f"{type(error).__name__}: {error}"}


def _gpu_cases(device: str, threads: int, seconds: float, memory_limit_bytes: int,
               deadline: float | None) -> dict[str, Any]:
    if torch is None or not torch.cuda.is_available():
        return {name: {"status": "unavailable", "reason": "CUDA unavailable in this PyTorch runtime"}
                for name in GPU_CASES + tuple(f"tensor-{x}-cuda" for x in GPU_TENSOR_FORMATS)}
    parsed = torch.device(device)
    index = torch.cuda.current_device() if parsed.index is None else parsed.index
    actual = f"cuda:{index}"
    torch.cuda.set_device(index)
    previous_threads = torch.get_num_threads()
    previous_tf32 = torch.backends.cuda.matmul.allow_tf32
    cases: dict[str, Any] = {}
    try:
        torch.set_num_threads(threads)
        # Strict FP32 must not inherit a user's process wide TF32 selection.
        torch.backends.cuda.matmul.allow_tf32 = False
        n = 1024
        for name, dtype in (("gemm-fp32-strict", torch.float32), ("gemm-fp64", torch.float64)):
            estimated = 3*n*n*torch.empty((), dtype=dtype).element_size()
            if estimated > memory_limit_bytes or estimated > int(torch.cuda.get_device_properties(index).total_memory*.5):
                cases[name] = {"status": "unsupported", "reason": "GEMM buffers exceed declared workspace or device-memory limit",
                               "estimated_workspace_bytes": estimated}
                continue
            a, b = torch.randn((n,n),dtype=dtype,device=actual), torch.randn((n,n),dtype=dtype,device=actual)
            cases[name] = _cuda_event_case(name, lambda a=a,b=b: torch.mm(a,b),
                lambda name=name,dtype=dtype,estimated=estimated,n=n: {
                    "work_kind": "dense square GPU GEMM", "shape": [n,n,n],
                    "estimated_operations_per_iteration": float(2*n*n*n),
                    "input_storage": str(dtype), "multiplication": str(dtype),
                    "accumulation": str(dtype), "output": str(dtype),
                    "strict_fp32_tf32_disabled": name == "gemm-fp32-strict",
                    "approximate_operand_and_output_bytes_per_iteration": estimated},
                device=actual, seconds=seconds, threads=threads, workspace=estimated, deadline=deadline)

        fft_n, batch = 65_536, 16
        for name, dtype in (("fft-complex64", torch.complex64), ("fft-complex128", torch.complex128)):
            estimated = 2*batch*fft_n*torch.empty((),dtype=dtype).element_size()
            if estimated > memory_limit_bytes or estimated > int(torch.cuda.get_device_properties(index).total_memory*.25):
                cases[name] = {"status": "unsupported", "reason": "FFT buffers exceed declared workspace or device-memory limit"}
                continue
            values = torch.ones((batch,fft_n),dtype=dtype,device=actual)
            fn = lambda values=values: torch.fft.fft(values,dim=-1)
            cases[name] = _cuda_event_case(name,fn,
                lambda name=name,dtype=dtype,estimated=estimated: {
                    "work_kind":"batched full-length GPU complex FFT", "shape":[batch,fft_n],
                    "estimated_operations_per_iteration":float(5*batch*fft_n*math.log2(fft_n)),
                    "input_storage":str(dtype),"multiplication":"backend FFT","accumulation":"backend FFT",
                    "output":str(dtype),"estimated_read_write_bytes_per_iteration":estimated},
                device=actual,seconds=seconds,threads=threads,workspace=estimated,deadline=deadline)

        # Two buffers larger than typical last-level caches when budget allows.
        # This is a measured working-set regime, not guaranteed DRAM bandwidth.
        stream_bytes = min(256*1024**2, memory_limit_bytes // 3)
        if 2*stream_bytes > memory_limit_bytes:
            cases["stream-copy"] = {"status":"unsupported","reason":"stream buffers exceed declared memory limit"}
        else:
            source = torch.ones(stream_bytes//4,dtype=torch.float32,device=actual)
            target = torch.empty_like(source)
            fn = lambda source=source,target=target: target.copy_(source)
            cases["stream-copy"] = _cuda_event_case("stream-copy",fn,
                lambda: {"work_kind":"repeated device-memory copy; cache/DRAM regime not independently verified","shape":[int(source.numel())],
                         "input_storage":"float32","multiplication":"none","accumulation":"none",
                         "output":"float32","bytes_moved_per_iteration":2*stream_bytes},
                device=actual,seconds=seconds,threads=threads,workspace=2*stream_bytes,deadline=deadline)

        n = 1_048_576
        source = torch.ones(n,dtype=torch.complex64,device=actual)
        phase = torch.exp(2j*math.pi*torch.arange(n,dtype=torch.float32,device=actual)/n).to(torch.complex64)
        if 3*n*8 > memory_limit_bytes:
            cases["phase-reduction-complex64"] = {"status":"unsupported","reason":"phase buffers exceed declared memory limit"}
        else:
            def phase_and_sum(source=source,phase=phase):
                phased = source*phase
                return torch.sum(torch.real(phased*torch.conj(phased)))
            cases["phase-reduction-complex64"] = _cuda_event_case("phase-reduction-complex64",phase_and_sum,
                lambda:{"work_kind":"samplewise phase product and full-array energy reduction","shape":[n],
                        "input_storage":"complex64","multiplication":"complex FP32","accumulation":"FP32 sum",
                        "output":"FP32 scalar","estimated_read_write_bytes_per_iteration":3*n*8},
                device=actual,seconds=seconds,threads=threads,workspace=3*n*8,deadline=deadline)

        batch,rows,cols=32,32,32
        estimate=batch*rows*cols*4*2+batch*rows*cols*4
        if estimate > memory_limit_bytes:
            cases["batched-svd-fp32"] = {"status":"unsupported","reason":"factor buffers exceed memory limit"}
        else:
            mats=torch.randn((batch,rows,cols),dtype=torch.float32,device=actual)
            fn=lambda mats=mats:torch.linalg.svdvals(mats)
            cases["batched-svd-fp32"]=_cuda_event_case("batched-svd-fp32",fn,
                lambda:{"work_kind":"batched small dense singular value solve","shape":[batch,rows,cols],
                        "input_storage":"float32","multiplication":"backend SVD","accumulation":"backend SVD",
                        "output":"float32 singular values","estimated_input_bytes_per_iteration":batch*rows*cols*4},
                device=actual,seconds=seconds,threads=threads,workspace=estimate,deadline=deadline)

        cases.update(_gpu_tensor_cases(actual,threads,seconds,memory_limit_bytes,deadline,index))
    finally:
        torch.backends.cuda.matmul.allow_tf32=previous_tf32
        torch.set_num_threads(previous_threads)
    return cases


def _gpu_tensor_cases(device: str, threads: int, seconds: float,
                      memory_limit_bytes: int, deadline: float | None,
                      device_index: int) -> dict[str, Any]:
    try:
        from triad_tensor_native import TensorBackend, capability
    except Exception as error:
        return {f"tensor-{fmt}-cuda": {"status":"unavailable","reason":f"tensor backend import failed ({type(error).__name__})"}
                for fmt in GPU_TENSOR_FORMATS}
    capability_hw = torch.cuda.get_device_capability(device_index)
    n=1024
    estimated=16*n*n*4
    cases={}
    for fmt in GPU_TENSOR_FORMATS:
        name=f"tensor-{fmt}-cuda"
        policy=capability(fmt,capability_hw,"triton")
        if not policy["supported"]:
            cases[name]={"status":"unsupported","reason":"format or Triton policy unsupported on this capability",
                         "requested_format":fmt,"capability":policy}
            continue
        if estimated > memory_limit_bytes or estimated > int(torch.cuda.get_device_properties(device_index).total_memory*.25):
            cases[name]={"status":"unsupported","reason":"tensor product exceeds declared memory limit"}
            continue
        if deadline is not None and time.monotonic() >= deadline:
            cases[name]={"status":"incomplete","reason":"profile deadline reached before tensor format"}
            continue
        try:
            backend=TensorBackend(format=fmt,implementation="triton",block_m=32,block_n=32,block_k=32,
                                  num_warps=4,num_stages=2,pack_implementation="triton")
            a=torch.randn((n,n),dtype=torch.float32,device=device).contiguous()
            b=torch.randn((n,n),dtype=torch.float32,device=device).contiguous()
            # Cold packed conversion/JIT/product time is retained independently;
            # profile wall time still includes import, prepare and all formats.
            cold=time.perf_counter()
            packed_a=backend.pack_rows(a); packed_b=backend.pack_rows(b)
            output=backend.matmul(packed_a,packed_b)
            torch.cuda.synchronize(device)
            cold_seconds=time.perf_counter()-cold
            reference=torch.mm(a.double(),b.double().T)
            error=float(torch.max(torch.abs(output.double()-reference))/torch.max(torch.abs(reference)).clamp_min(1e-30))
            if not bool(torch.isfinite(output).all()) or not math.isfinite(error):
                raise FloatingPointError("nonfinite tensor product or FP64 diagnostic")
            fn=lambda backend=backend,packed_a=packed_a,packed_b=packed_b:backend.matmul(packed_a,packed_b)
            case=_cuda_event_case(name,fn,
                lambda fmt=fmt,n=n,error=error: {
                    "work_kind":"isolated TensorBackend GEMM capability stress, not receiver admission",
                    "shape":[n,n,n],"estimated_operations_per_iteration":float(2*n*n*n),
                    "requested_format":fmt,"requested_engine":"Triton",
                    "input_storage":"FP32 input packed/scaled to requested tensor format",
                    "multiplication":fmt,"accumulation":"FP32" if fmt!="int8" else "INT32 then FP32 restore",
                    "output":"FP32","precision_exceptions":["FP32 row scaling","FP32 or INT32 accumulation","FP64 reference/error diagnostic is setup-only", "timed per-output FP64 scale restoration multiply then FP32 cast"],
                    "measured_scaled_small_product_relative_error_max":error,
                    "receiver_sensitivity_qualified":False,
                    "tensor_instruction_evidence":dict(next(iter(backend._artifacts.values()))["evidence"]),
                    "capability":policy, "backend_contract":backend.contract_metadata(),
                    "backend_runtime":backend.runtime_metadata(),
                    "saturation_verified":False},
                device=device,seconds=seconds,threads=threads,workspace=estimated,deadline=deadline)
            case["cold_pack_compile_first_product_seconds"]=cold_seconds
            case["requested_arithmetic"]={"input":"FP32","multiplication":fmt,
                                          "accumulation":"backend contract; see precision fields",
                                          "output":"FP32","engine":"Triton"}
            cases[name]=case
        except (RuntimeError,ValueError,MemoryError,FloatingPointError) as error:
            # A format capability probe must reject compiler substitutions.
            # Missing the requested instruction is unsupported on this exact
            # stack, not measured throughput for a different arithmetic mode.
            rejected_instruction = isinstance(error, RuntimeError) and "lacks matching dense tensor instruction" in str(error)
            cases[name]={"status":"unsupported" if rejected_instruction else "failed","requested_format":fmt,
                         "reason":f"{type(error).__name__}: {error}",
                         "instruction_verification_rejected": rejected_instruction,
                         "fallback_executed":False}
    return cases


def collect_profile(*, device: str = "cpu", threads: int = 1,
                    seconds: float = 5.0,
                    memory_limit_bytes: int = DEFAULT_WORKSPACE_BYTES,
                    deadline: float | None = None) -> dict[str, Any]:
    """Run bounded saturated kernel families, returning a JSON-ready measurement.

    `seconds` is the target duration of each warmed arithmetic case, not a
    count of independent CI replications. `deadline`, when supplied, is an
    absolute `time.monotonic()` limit checked between bounded operations.
    """
    _validate(device,threads,seconds,memory_limit_bytes)
    if deadline is not None and (isinstance(deadline,bool) or not isinstance(deadline,(int,float)) or not math.isfinite(deadline)):
        raise ValueError("deadline must be an absolute finite monotonic time")
    started=time.perf_counter()
    telemetry = None
    if device == "cpu":
        cases = _cpu_cases(threads,seconds,memory_limit_bytes,deadline)
    else:
        from triad_perf_telemetry import start_sampler
        index = (int(device.split(":", 1)[1]) if ":" in device else
                 torch.cuda.current_device() if torch is not None and torch.cuda.is_available() else 0)
        selected_uuid = None
        if torch is not None and torch.cuda.is_available():
            value = str(getattr(torch.cuda.get_device_properties(index), "uuid", ""))
            if not value.startswith("GPU-"):
                value = "GPU-" + value
            if re.fullmatch(r"GPU-[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}", value):
                selected_uuid = value
        context = (start_sampler(index, inherit_process_group=True, device_uuid=selected_uuid)
                   if selected_uuid else contextlib.nullcontext(None))
        with context as sampler:
            telemetry_start = time.monotonic()
            cases = _gpu_cases(device,threads,seconds,memory_limit_bytes,deadline)
            telemetry_end = time.monotonic()
        for case in cases.values():
            window = case.pop("telemetry_window_monotonic", None)
            if window and sampler is not None:
                case["device_busy_telemetry"] = sampler.summarize(*window)
        telemetry = (sampler.summarize(telemetry_start, telemetry_end) if sampler is not None else
                     {"status": "unavailable", "reason": "selected CUDA UUID unavailable; logical-to-physical index mapping not assumed", "cleanup": "not_started"})

    statuses={record.get("status") for record in cases.values()}
    if telemetry and telemetry.get("cleanup", "").startswith("unavailable"):
        overall="incomplete"
    elif "failed" in statuses:
        overall="failed"
    elif "incomplete" in statuses:
        overall="incomplete"
    elif cases and all(status in ("unavailable","unsupported") for status in statuses):
        overall="unavailable"
    else:
        overall="complete"
    return {"schema":SCHEMA,"status":overall,"confidence_interval":None,
            "confidence_note":"microkernel iterations within this process are not independent replications; no confidence interval is calculated",
            "receiver_precision_selection":"not performed; pair with strict receiver-output qualification",
            "device":device,"requested_seconds_per_case":float(seconds),
            "memory_limit_bytes":memory_limit_bytes,
            "memory_scope":"estimated per-case arrays; retained outputs, allocator caching and backend workspaces can increase whole-process peak",
            "hardware":discover_hardware(device,threads),
            "cases":cases,"device_busy_telemetry":telemetry,
            "total_profile_wall_seconds":time.perf_counter()-started,
            "deadline_monotonic":deadline,"workload_scope":"hardware capability/health probes; not a receiver-time prediction"}


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device",default="cpu",help="cpu or cuda[:index]; GPU unavailability is explicit")
    parser.add_argument("--threads",type=int,default=1)
    parser.add_argument("--seconds",type=float,default=5.)
    parser.add_argument("--memory-mib",type=float,default=512.)
    parser.add_argument("--output",type=Path,help="new JSON output path; stdout by default")
    args=parser.parse_args(argv)
    if not math.isfinite(args.memory_mib) or args.memory_mib <= 0:
        parser.error("--memory-mib must be finite and positive")
    _validate(args.device,args.threads,args.seconds,int(args.memory_mib*1024*1024))
    return args


def main(argv: list[str] | None = None) -> int:
    args=parse_args(argv)
    profile=collect_profile(device=args.device,threads=args.threads,seconds=args.seconds,
                            memory_limit_bytes=int(args.memory_mib*1024*1024))
    if args.output:
        if args.output.exists():
            raise ValueError("refusing to overwrite profile output")
        args.output.parent.mkdir(parents=True,exist_ok=True)
        with args.output.open("x") as handle:
            json.dump(profile,handle,indent=2,allow_nan=False)
            handle.write("\n")
    else:
        json.dump(profile,sys.stdout,indent=2,allow_nan=False)
        sys.stdout.write("\n")
    return 0 if profile["status"] in ("complete","unavailable") else 2


if __name__ == "__main__":  # pragma: no cover - exercised by smoke CLI only
    try:
        raise SystemExit(main())
    except (ValueError,OSError,MemoryError) as error:
        print(f"triad_perf_hardware: {error}",file=sys.stderr)
        raise SystemExit(2)

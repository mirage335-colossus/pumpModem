#!/usr/bin/env python3
"""Bounded single-process native FP4 shape/tile engineering sweep.

Inputs are synthetic FP32 engineering matrices, not received waveforms.
Every operation is warmed before timing.  Host-paced CUDA-event and wall
timings include launch/allocator gaps; static-output unrolled CUDA graphs
measure products with amortized host launches.  Packing, cached-template
pipeline, and both-operands pipeline are charged separately.  Operation and
byte estimates below are not measured hardware utilization counters.
"""
from __future__ import annotations

import argparse
import gc
import hashlib
import json
from pathlib import Path
import platform
import time
import traceback

import torch

import triad_fp4_native as native


def _tuples(text, count):
    values = [tuple(map(int, part.split(","))) for part in text.split(";")]
    if not values or any(len(item) != count or min(item) < 1 for item in values):
        raise argparse.ArgumentTypeError(f"need semicolon-separated {count}-integer tuples")
    return values


def _record(backend, a, b, *, repeats, graph_calls, artifacts):
    m, k = a.shape
    n = b.shape[0]
    record = {"shape_mnk": [m, n, k], "format": backend.format,
              "tile": [backend.block_m, backend.block_n, backend.block_k,
                       backend.num_warps, backend.num_stages]}
    first = time.perf_counter()
    qa, qb = backend.pack_rows(a), backend.pack_rows(b)
    output = backend.matmul(qa, qb)
    torch.cuda.synchronize()
    record["cold_native_pack_compile_product_ms"] = (time.perf_counter()-first)*1000
    strict_output = torch.empty_like(output)
    first = time.perf_counter()
    torch.mm(a, b.T, out=strict_output)
    torch.cuda.synchronize()
    record["first_strict_use_ms"] = (time.perf_counter()-first)*1000
    operations = {
        "pack_a": lambda: backend.pack_rows(a),
        "pack_b": lambda: backend.pack_rows(b),
        "native_product": lambda: backend.matmul(qa, qb, out=output),
        "strict_fp32_product": lambda: torch.mm(a, b.T, out=strict_output),
        "cached_a_pack_b_product": lambda: backend.matmul(qa, backend.pack_rows(b), out=output),
        "pack_both_product": lambda: backend.matmul(backend.pack_rows(a), backend.pack_rows(b), out=output),
    }
    for operation in operations.values():
        operation()
        operation()
    torch.cuda.synchronize()
    record["host_paced"] = {name: native._timed(operation, repeats)[0]
                            for name, operation in operations.items()}
    record["product_graph"] = {
        "native_fp4": native.graph_timed(operations["native_product"], calls=graph_calls),
        "strict_fp32": native.graph_timed(operations["strict_fp32_product"], calls=graph_calls)}
    full = m*n <= 2**20 and m*n*k <= 2**26
    am, bn = (m, n) if full else (min(m, 32), min(n, 32))
    da = backend.decode(native.subset_rows(qa, am)).cpu()
    db = backend.decode(native.subset_rows(qb, bn)).cpu()
    ref = da@db.T
    y = output[:am, :bn].cpu().double()
    bound = (k*2.**-24)/(1-k*2.**-24)*(da.abs()@db.abs().T)
    absolute = (y-ref).abs()
    strict_ref = a[:am].cpu().double()@b[:bn].cpu().double().T
    record["arithmetic"] = {
        "scope": "full_output" if full else "first_rows_submatrix",
        "checked_shape": [am, bn, k], "reference": "CPU FP64 dequantized operands",
        "native_vs_represented_max_abs": absolute.max().item(),
        "native_vs_represented_rms": (y-ref).square().mean().sqrt().item(),
        "gamma_k_bound_max": bound.max().item(),
        "pass_conservative_fp32_bound": bool((absolute <= 2e-5+8*bound).all()),
        "representation_vs_original_rms": (ref-strict_ref).square().mean().sqrt().item(),
        "strict_fp32_vs_original_max_abs": (strict_output[:am, :bn].cpu().double()-strict_ref).abs().max().item()}
    record["packed"] = {"a_bytes": qa.nbytes, "b_bytes": qb.nbytes,
                        "a_padded_shape": qa.padded_shape, "b_padded_shape": qb.padded_shape,
                        "a_diagnostics_sum": qa.diagnostics.sum(0).cpu().tolist(),
                        "b_diagnostics_sum": qb.diagnostics.sum(0).cpu().tolist()}
    record["instruction_evidence"] = backend.instruction_evidence
    record["kernel_metadata"] = str(backend.last_kernel.metadata)
    record["kernel_artifacts"] = native._save_kernel(backend.last_kernel, artifacts)
    ops = 2*m*n*k
    record["operation_count_estimate"] = ops
    record["dense_tflops_from_operation_count"] = {
        name: ops/(result["cuda_ms_per_call"]*1e9) for name, result in record["product_graph"].items()}
    record["hardware_counters"] = None
    record["status"] = "passed" if record["arithmetic"]["pass_conservative_fp32_bound"] else "failed"
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shapes", default="4096,4096,512;4096,4096,4096;4096,4096,16384")
    parser.add_argument("--tiles", default="128,128,128,4,2;128,256,128,4,2;128,128,256,4,2")
    parser.add_argument("--formats", default="nvfp4")
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--graph-calls", type=int, choices=(8, 16, 32, 64), default=32)
    parser.add_argument("--seed", type=int, default=77101)
    parser.add_argument("--max-seconds", type=float, default=600)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    args = parser.parse_args()
    shapes, tiles, formats = _tuples(args.shapes, 3), _tuples(args.tiles, 5), args.formats.split(",")
    if not 1 <= args.repeats <= 100 or not 1 <= args.max_seconds <= 1800 or len(shapes)*len(tiles)*len(formats) > 72:
        parser.error("bounded repeats/time/case count required")
    if any(f not in native.FORMATS for f in formats):
        parser.error("formats must be nvfp4 and/or mxfp4")
    if any(k > 32768 or m*n > 2**28 or (m+n)*k > 2**28 for m, n, k in shapes):
        parser.error("shape exceeds bounded matrix allocation")
    report = {"schema": "triad-native-fp4-sweep-v1", "seed": args.seed,
              "source_hashes": {str(path.name): hashlib.sha256(path.read_bytes()).hexdigest()
                                for path in (Path(__file__), Path(native.__file__))},
              "environment": {"python": platform.python_version(), "torch": torch.__version__,
                              "cuda_runtime": torch.version.cuda},
              "timing_scope": "synthetic engineering matrices; no receiver, sensitivity or admission validation",
              "cases": [], "status": "running"}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    def save():
        args.output.write_text(json.dumps(report, indent=2)+"\n")
    save()
    started = time.perf_counter()
    try:
        if not torch.cuda.is_available():
            raise RuntimeError("CUDA unavailable; no fallback executed")
        import triton
        report["environment"].update(triton=triton.__version__, gpu=torch.cuda.get_device_name(),
                                     capability=torch.cuda.get_device_capability())
        torch.set_float32_matmul_precision("highest")
        torch.backends.cuda.matmul.allow_tf32 = False
        generator = torch.Generator(device="cuda").manual_seed(args.seed)
        for m, n, k in shapes:
            if time.perf_counter()-started >= args.max_seconds:
                report["status"] = "time_limit_before_next_shape"
                break
            free, total = torch.cuda.mem_get_info()
            estimate = (m+n)*k*5+2*m*n*4+512*1024**2
            if estimate > free*.75:
                report["cases"].append({"shape_mnk": [m, n, k], "status": "memory_budget_skip",
                                         "estimated_bytes": estimate, "free_bytes": free})
                save()
                continue
            rng_started = time.perf_counter()
            a = torch.randn((m, k), device="cuda", dtype=torch.float32, generator=generator)
            b = torch.randn((n, k), device="cuda", dtype=torch.float32, generator=generator)
            torch.cuda.synchronize()
            rng_seconds = time.perf_counter()-rng_started
            for format in formats:
                for tile in tiles:
                    if time.perf_counter()-started >= args.max_seconds:
                        report["status"] = "time_limit_before_next_case"
                        break
                    case_started = time.perf_counter()
                    name = f"{format}-m{m}-n{n}-k{k}-tile{'x'.join(map(str, tile))}"
                    try:
                        backend = native.NativeFp4Backend(format, block_m=tile[0], block_n=tile[1],
                                                           block_k=tile[2], num_warps=tile[3], num_stages=tile[4])
                        case = _record(backend, a, b, repeats=args.repeats, graph_calls=args.graph_calls,
                                       artifacts=args.artifacts/name)
                    except Exception as error:
                        case = {"shape_mnk": [m, n, k], "format": format, "tile": tile,
                                "status": "failed", "error": f"{type(error).__name__}: {error}",
                                "traceback": traceback.format_exc()}
                    case["shape_input_rng_allocation_seconds"] = rng_seconds
                    case["complete_case_seconds_excluding_shared_rng"] = time.perf_counter()-case_started
                    report["cases"].append(case)
                    save()
                    print(json.dumps({"case": name, "status": case["status"]}), flush=True)
            del a, b
            gc.collect()
            torch.cuda.empty_cache()
        if report["status"] == "running":
            report["status"] = "completed"
    except Exception as error:
        report["status"] = "failed"
        report["error"] = f"{type(error).__name__}: {error}"
        report["traceback"] = traceback.format_exc()
    report["complete_sweep_seconds"] = time.perf_counter()-started
    save()
    return 0 if report["status"] == "completed" and all(c["status"] == "passed" for c in report["cases"]) else 2


if __name__ == "__main__":
    raise SystemExit(main())

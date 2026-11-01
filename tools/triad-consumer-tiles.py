#!/usr/bin/env python3
"""Bounded FP16 Triton tile sweep for large dense correlation products.

This is a product/packing microbenchmark. It does not run the phase receiver,
change precision defaults, or establish complete-receiver throughput.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import platform
from pathlib import Path
import subprocess
import sys
import time
import traceback
from typing import Any, Callable

try:
    import torch
except Exception as error:  # pragma: no cover - runtime environment dependent
    torch = None
    _TORCH_IMPORT_ERROR = f"{type(error).__name__}: {error}"
else:
    _TORCH_IMPORT_ERROR = None


SEED = 4_729_019
WARMUPS = 3
REPEATS = 7
MAX_SECONDS = 240.0
CONFIGS = (
    (64, 64, 32, 4), (128, 64, 32, 4), (64, 128, 32, 4),
    (128, 128, 32, 4), (128, 128, 64, 4), (128, 128, 64, 8),
    (128, 256, 64, 8), (256, 128, 64, 8),
)
SHAPES = ((2048, 1024, 4096), (4096, 256, 512))  # M,N,K


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tensor_hash(value: Any) -> str:
    data = value.contiguous().numpy()
    return hashlib.sha256(memoryview(data).cast("B")).hexdigest()


def load_tensor_module(root: Path):
    source = root / "tools" / "triad_tensor_native.py"
    if not source.is_file():
        raise FileNotFoundError(source)
    sys.path.insert(0, str(source.parent.resolve()))
    spec = importlib.util.spec_from_file_location("triad_consumer_tiles_tensor", source)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not import triad_tensor_native.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module, source


def strict_fp32_controls() -> None:
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    torch.set_float32_matmul_precision("highest")
    for name in ("allow_fp16_reduced_precision_reduction",
                 "allow_bf16_reduced_precision_reduction",
                 "allow_fp16_accumulation"):
        if hasattr(torch.backends.cuda.matmul, name):
            setattr(torch.backends.cuda.matmul, name, False)
    if torch.backends.cuda.matmul.allow_tf32:
        raise RuntimeError("strict FP32 control did not disable TF32")


def synchronize(device: Any) -> None:
    torch.cuda.synchronize(device)


def summarize(values: list[float]) -> dict[str, Any]:
    ordered = sorted(values)
    return {"samples_seconds": values, "mean_seconds": sum(values)/len(values),
            "median_seconds": ordered[len(ordered)//2], "min_seconds": min(values),
            "max_seconds": max(values)}


def measure(function: Callable[[], Any], device: Any) -> tuple[dict[str, Any], Any]:
    for _ in range(WARMUPS):
        value = function()
        synchronize(device)
        del value
    walls: list[float] = []
    events: list[float] = []
    final = None
    for _ in range(REPEATS):
        synchronize(device)
        start_event = torch.cuda.Event(enable_timing=True)
        stop_event = torch.cuda.Event(enable_timing=True)
        wall_start = time.perf_counter()
        start_event.record()
        final = function()
        stop_event.record()
        stop_event.synchronize()
        walls.append(time.perf_counter()-wall_start)
        events.append(float(start_event.elapsed_time(stop_event))/1000.)
    return {"repeats": REPEATS, "warmups": WARMUPS,
            "wall": summarize(walls), "cuda_event": summarize(events)}, final


def backend_product(backend: Any, packed_a: Any, packed_b: Any) -> Any:
    return backend.matmul(packed_a, packed_b)


def artifact_export(backend: Any, directory: Path) -> list[dict[str, Any]]:
    return backend.write_artifacts(directory)


def configuration_directory(output_dir: Path, config_name: str) -> Path:
    config_dir = output_dir / config_name
    config_dir.mkdir(parents=True, exist_ok=False)
    return config_dir


def run_configuration(tensor_module: Any, device: Any, a_device: Any,
                      b_device: Any, tile: tuple[int, int, int, int],
                      output_dir: Path) -> dict[str, Any]:
    bm, bn, bk, warps = tile
    config_name = f"bm{bm}-bn{bn}-bk{bk}-w{warps}"
    config_dir = configuration_directory(output_dir, config_name)
    constructor_supported = bm <= 128 and bn <= 128
    record: dict[str, Any] = {
        "configuration": {"block_m": bm, "block_n": bn, "block_k": bk,
                          "num_warps": warps, "num_stages": 2,
                          "format": "fp16", "accumulation": "native",
                          "pack_implementation": current_pack_implementation,
                          "backend_constructor_supports_tile": constructor_supported},
        "status": "running",
    }
    if not constructor_supported:
        record.update(status="unsupported_tile",
                      reason="TensorBackend constructor supports BM and BN no larger than 128; no private-kernel bypass is used")
        (config_dir / "record.json").write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
        return record
    try:
        backend = tensor_module.TensorBackend(
            format="fp16", implementation="triton",
            block_m=bm, block_n=bn, block_k=bk, num_warps=warps, num_stages=2,
            pack_implementation=current_pack_implementation)
        setup_start = time.perf_counter()
        packed_a = backend.pack_rows(a_device)
        packed_b = backend.pack_rows(b_device)
        # First product compiles/dispatches the requested tile and verifies the
        # PTX before any timing rows can be recorded.
        native_result = backend_product(backend, packed_a, packed_b)
        synchronize(device)
        record["cold_pack_and_product_seconds"] = time.perf_counter()-setup_start
        record["backend_contract"] = backend.contract_metadata()
        record["backend_runtime"] = backend.runtime_metadata()

        decoded_a = tensor_module.decode_rows(packed_a, dtype=torch.float64)[:8].cpu()
        decoded_b = tensor_module.decode_rows(packed_b, dtype=torch.float64)[:8].cpu()
        oracle = decoded_a @ decoded_b.T
        actual = native_result[:8, :8].detach().cpu().double()
        delta = (actual-oracle).abs()
        record["represented_fp64_oracle_8x8"] = {
            "input": "CPU FP64 product of decoded, actually packed FP16 operands",
            "max_abs_error": float(delta.max()),
            "max_relative_error": float((delta/oracle.abs().clamp_min(1e-30)).max()),
            "allclose_atol_5e-4_rtol_2e-4": bool(torch.allclose(actual, oracle, atol=5e-4, rtol=2e-4)),
            "native_output_dtype": str(native_result.dtype),
            "native_ptx_evidence": backend.runtime_metadata()["native_ptx_verified"],
        }
        if (not bool(torch.isfinite(actual).all()) or
                not bool(torch.isfinite(oracle).all()) or
                not record["represented_fp64_oracle_8x8"]["allclose_atol_5e-4_rtol_2e-4"]):
            raise RuntimeError("FP16 native 8x8 result failed finite-value or declared FP64-oracle tolerance gate")

        def cached_product():
            return backend_product(backend, packed_a, packed_b)

        def cached_a_fresh_b_pack_product():
            fresh_b = backend.pack_rows(b_device)
            return backend_product(backend, packed_a, fresh_b)

        def both_pack_product():
            fresh_a = backend.pack_rows(a_device)
            fresh_b = backend.pack_rows(b_device)
            return backend_product(backend, fresh_a, fresh_b)

        record["stages"] = {}
        for label, function in (("cached_product", cached_product),
                                ("cached_A_fresh_B_pack_product", cached_a_fresh_b_pack_product),
                                ("both_pack_product", both_pack_product)):
            measurement, _ = measure(function, device)
            record["stages"][label] = measurement

        def strict_fp32_product():
            return torch.mm(a_device, b_device.T)

        fp32_measurement, fp32_result = measure(strict_fp32_product, device)
        record["stages"]["strict_fp32_allocate_output_product"] = fp32_measurement
        fp32_error = (fp32_result[:8, :8].detach().cpu().double()-
                      (a_device[:8].cpu().double() @ b_device[:8].cpu().double().T)).abs()
        record["strict_fp32_reference_check_8x8"] = {
            "max_abs_error": float(fp32_error.max()),
            "tf32_allowed": torch.backends.cuda.matmul.allow_tf32,
            "float32_matmul_precision": torch.get_float32_matmul_precision(),
        }
        artifacts = artifact_export(backend, config_dir / "artifacts")
        record["artifacts"] = artifacts
        record["status"] = "completed"
    except Exception as error:
        record.update(status="failed", error=f"{type(error).__name__}: {error}",
                      traceback=traceback.format_exc())
    (config_dir / "record.json").write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
    return record


current_pack_implementation = "triton"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--device", default="cuda:0")
    parser.add_argument("--output-dir", type=Path, required=True,
                        help="new result directory; existing directories are not overwritten")
    parser.add_argument("--pack-implementation", choices=("torch", "triton"), default="triton")
    parser.add_argument("--max-seconds", type=float, default=MAX_SECONDS)
    args = parser.parse_args()
    if torch is None:
        parser.error(f"PyTorch is required for this CUDA experiment: {_TORCH_IMPORT_ERROR}")
    if not 1 <= args.max_seconds <= MAX_SECONDS:
        parser.error(f"--max-seconds must be in [1,{MAX_SECONDS:g}]")
    root, output_dir = args.root.resolve(), args.output_dir.resolve()
    if output_dir.exists():
        parser.error(f"output directory must be new: {output_dir}")
    try:
        output_dir.relative_to(root)
    except ValueError:
        parser.error("--output-dir must be under --root")
    global current_pack_implementation
    current_pack_implementation = args.pack_implementation
    if not torch.cuda.is_available():
        parser.error("CUDA is required; CPU fallback is prohibited")
    device = torch.device(args.device)
    if device.type != "cuda":
        parser.error("--device must select CUDA")
    torch.cuda.set_device(device)
    strict_fp32_controls()
    tensor_module, tensor_source = load_tensor_module(root)
    output_dir.mkdir(parents=True)
    try:
        import triton
        triton_version = getattr(triton, "__version__", "unknown")
    except Exception as error:
        triton_version = f"unavailable: {type(error).__name__}: {error}"
    properties = torch.cuda.get_device_properties(device)
    common = {
        "schema": "triad-consumer-tiles-v1", "status": "running",
        "source_hashes": {"triad-consumer-tiles.py": sha256(Path(__file__).resolve()),
                          "triad_tensor_native.py": sha256(tensor_source)},
        "environment": {"python": platform.python_version(), "torch": str(torch.__version__),
            "torch_cuda_build": torch.version.cuda, "triton": triton_version,
            "device": str(device), "gpu_name": str(properties.name),
            "compute_capability": [properties.major, properties.minor],
            "total_memory_bytes": int(properties.total_memory),
            "multiprocessor_count": int(properties.multi_processor_count),
            "tf32_allowed": torch.backends.cuda.matmul.allow_tf32,
            "float32_matmul_precision": torch.get_float32_matmul_precision()},
        "seed": SEED, "maximum_seconds": args.max_seconds,
        "arguments": {"root": ".", "device": str(device),
                      "output_directory": output_dir.relative_to(root).as_posix(),
                      "pack_implementation": args.pack_implementation,
                      "max_seconds": args.max_seconds},
        "warmups": WARMUPS, "repeats": REPEATS,
        "shapes_MNK": [list(shape) for shape in SHAPES],
        "tile_configs_BM_BN_BK_warps": [list(config) for config in CONFIGS],
        "timing_claim": "microbenchmark only; no P512 receiver, end-to-end throughput, or admission claim",
        "records": [],
    }
    index_path = output_dir / "index.json"
    index_path.write_text(json.dumps(common, indent=2, sort_keys=True) + "\n")
    campaign_start = time.perf_counter()
    global_record_number = 0
    for shape_number, (m, n, k) in enumerate(SHAPES):
        shape_seed = SEED + shape_number
        generator = torch.Generator(device="cpu").manual_seed(shape_seed)
        a_cpu = torch.randn((m, k), generator=generator, dtype=torch.float32)
        b_cpu = torch.randn((n, k), generator=generator, dtype=torch.float32)
        shape_meta = {"shape_MNK": [m, n, k], "seed": shape_seed,
                      "cpu_A_sha256": tensor_hash(a_cpu), "cpu_B_sha256": tensor_hash(b_cpu),
                      "array_source": "single fixed-seed CPU FP32 arrays copied unchanged to GPU for every tile config"}
        a_device, b_device = a_cpu.to(device), b_cpu.to(device)
        torch.cuda.synchronize(device)
        for tile in CONFIGS:
            elapsed = time.perf_counter()-campaign_start
            if elapsed >= args.max_seconds:
                remaining = [(m, n, k, *cfg) for m, n, k in SHAPES for cfg in CONFIGS][global_record_number:]
                for skipped in remaining:
                    common["records"].append({"status": "skipped_budget", "shape_MNK": skipped[:3],
                                              "tile_BM_BN_BK_warps": skipped[3:]})
                common["status"] = "incomplete_budget"
                break
            conf = (f"shape-{m}-{n}-{k}-"+
                    f"bm{tile[0]}-bn{tile[1]}-bk{tile[2]}-w{tile[3]}")
            record = {**shape_meta, "tile_BM_BN_BK_warps": list(tile), "status": "running"}
            common["records"].append(record)
            index_path.write_text(json.dumps(common, indent=2, sort_keys=True) + "\n")
            result = run_configuration(tensor_module, device, a_device, b_device,
                                       tile, output_dir / conf)
            record.update(result)
            common["elapsed_seconds"] = time.perf_counter()-campaign_start
            index_path.write_text(json.dumps(common, indent=2, sort_keys=True) + "\n")
            global_record_number += 1
        if common["status"] == "incomplete_budget":
            break
        del a_device, b_device, a_cpu, b_cpu
        torch.cuda.synchronize(device)
    if common["status"] == "running":
        statuses = [row.get("status") for row in common["records"]]
        if all(status in ("completed", "unsupported_tile") for status in statuses):
            common["status"] = ("complete_with_unsupported_tiles"
                                if "unsupported_tile" in statuses else "complete")
        else:
            common["status"] = "completed_with_failures"
    common["elapsed_seconds"] = time.perf_counter()-campaign_start
    common["host_strict_fp32_policy"] = "TF32 disabled; FP16 reduced accumulation disabled"
    index_path.write_text(json.dumps(common, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"status": common["status"], "output": str(index_path),
                      "elapsed_seconds": common["elapsed_seconds"],
                      "records": len(common["records"])}, sort_keys=True))
    return 0 if common["status"] in ("complete", "complete_with_unsupported_tiles") else 1


if __name__ == "__main__":
    raise SystemExit(main())

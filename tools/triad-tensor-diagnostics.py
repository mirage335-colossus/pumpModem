#!/usr/bin/env python3
"""Bounded CUDA screening for the experimental triad frequency contraction.

This is a standalone diagnostic, not a receiver admission or throughput test.
It requires native CUDA + Triton and never falls back to CPU for execution.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import statistics
import sys
import time
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

DEFAULT_PATTERNS = ("uniform", "concentration", "logspace", "subnormal")
DEFAULT_BATCHES = (1, 4, 12)
DEFAULT_BLOCK_COLUMNS = (64, 128, 256)
TENSOR_MODES = ("strict-padded", "tf32", "tf32x3", "bf16", "fp16-scaled", "bf16-split2")
BLOCK_COLUMNS = (32, 64, 128, 256)
torch = None
PhaseFrequencyLikelihood = None
PhaseFrequencyModel = None
cpu_frequency_reference = None
frequency_contract = None
operand_diagnostics = None


def comma_values(text, allowed, label):
    values = tuple(x.strip() for x in text.split(",") if x.strip())
    if not values or any(x not in allowed for x in values):
        raise argparse.ArgumentTypeError(f"{label} must be a comma-separated subset of {','.join(map(str, allowed))}")
    if len(set(values)) != len(values):
        raise argparse.ArgumentTypeError(f"{label} entries must be unique")
    return values


def batches_value(text):
    try:
        values = tuple(int(x) for x in text.split(","))
    except ValueError as error:
        raise argparse.ArgumentTypeError("batches must be positive comma-separated integers") from error
    if not values or any(x < 1 for x in values) or len(set(values)) != len(values):
        raise argparse.ArgumentTypeError("batches must be unique positive integers")
    return values


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", required=True, type=Path, help="fresh JSON output path")
    p.add_argument("--batches", type=batches_value, default=DEFAULT_BATCHES)
    p.add_argument("--patterns", type=lambda s: comma_values(s, DEFAULT_PATTERNS, "patterns"), default=DEFAULT_PATTERNS)
    p.add_argument("--modes", type=lambda s: comma_values(s, TENSOR_MODES, "modes"), default=TENSOR_MODES)
    p.add_argument("--block-columns", type=lambda s: tuple(int(x) for x in comma_values(s, tuple(map(str, DEFAULT_BLOCK_COLUMNS)), "block-columns")), default=DEFAULT_BLOCK_COLUMNS)
    p.add_argument("--repeats", type=int, default=50, help="CUDA event repetitions after 10 warmups")
    p.add_argument("--seed", type=int, default=20261101)
    return p


def derived_seed(seed, pattern_index, batch):
    material = f"{seed}:{pattern_index}:{batch}".encode("ascii")
    return int.from_bytes(hashlib.sha256(material).digest()[:8], "little") % (2**63 - 1)


def make_values(pattern, batch, seed, *, frequency, phases, quadrature, candidates=2):
    shape = (batch, candidates, quadrature, frequency, phases)
    generator = torch.Generator(device="cpu").manual_seed(seed)
    n = quadrature * frequency * phases
    if pattern == "uniform":
        values = torch.ones(shape, dtype=torch.float64)
    elif pattern == "concentration":
        # Independent random weights with a 30-neper dynamic range.
        values = torch.exp(-30*torch.rand(shape, generator=generator, dtype=torch.float64))
    elif pattern == "logspace":
        base = torch.exp(torch.linspace(0., -90., n, dtype=torch.float64)).reshape(1, 1, quadrature, frequency, phases)
        values = base.expand(shape).clone()
    elif pattern == "subnormal":
        base = torch.exp(torch.linspace(0., -104., n, dtype=torch.float64)).reshape(1, 1, quadrature, frequency, phases)
        base[..., ::97] = 0.
        values = base.expand(shape).clone()
    else:
        raise ValueError(f"unknown pattern {pattern}")
    # Define each [trial,candidate] posterior mass in FP64 before FP32 storage.
    totals = values.sum(dim=(-3, -2, -1), keepdim=True)
    return (values/totals).to(torch.float32).contiguous()


def row_metrics(actual, expected):
    if not bool(torch.isfinite(actual).all()) or not bool(torch.isfinite(expected).all()):
        raise FloatingPointError("nonfinite contraction output/reference; failed case retained")
    a = actual.detach().double().reshape(actual.shape[0], actual.shape[1], -1)
    e = expected.detach().double().reshape(expected.shape[0], expected.shape[1], -1)
    delta = (a-e).abs()
    denom = e.abs().sum().clamp_min(torch.finfo(torch.float64).tiny)
    mass_a, mass_e = a.sum(-1), e.sum(-1)
    return {
        "finite": bool(torch.isfinite(actual).all()),
        "max_absolute_error": float(delta.max()),
        "relative_l1_error": float(delta.sum()/denom),
        "maximum_absolute_mass_row_error": float((mass_a-mass_e).abs().max()),
        "max_absolute_value": float(actual.detach().abs().max()),
    }


def timed_cuda(fn, repeats):
    for _ in range(10):
        temporary = fn()
        del temporary
    torch.cuda.synchronize()
    starts = [torch.cuda.Event(enable_timing=True) for _ in range(repeats)]
    ends = [torch.cuda.Event(enable_timing=True) for _ in range(repeats)]
    wall_started = time.perf_counter()
    for index in range(repeats):
        starts[index].record()
        temporary = fn()
        ends[index].record()
        del temporary
    torch.cuda.synchronize()
    samples = [start.elapsed_time(end)/1000. for start, end in zip(starts, ends)]
    return {"repeats": repeats, "warmups": 10, "median_seconds": statistics.median(samples),
            "min_seconds": min(samples), "max_seconds": max(samples),
            "host_loop_including_final_sync_seconds_per_call": (time.perf_counter()-wall_started)/repeats,
            "scope": "CUDA elapsed including possible launch gaps; conversion is in kernel; host loop wall includes allocation, launch, events and final sync"}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def instruction_counts(text, sass=False):
    if sass:
        patterns = {"tensor_core": r"\b(?:H?MMA|HMMA|IMMA|BMMA)\b", "load": r"\bLDG\w*\b",
                    "store": r"\bSTG\w*\b", "fp32_fma": r"\bFFMA\b", "conversion": r"\b(?:F2F|I2F|F2I)\w*\b"}
    else:
        patterns = {"dot_or_mma": r"\b(?:mma|dot)\.", "load": r"\bld\.global\b",
                    "store": r"\bst\.global\b", "convert": r"\bcvt\."}
    return {key: len(re.findall(pattern, text, flags=re.IGNORECASE)) for key, pattern in patterns.items()}


def save_kernel_artifacts(compiled, asm_root, key):
    result = {"available": [], "files": {}, "instruction_counts": {}, "metadata": {}}
    asm = getattr(compiled, "asm", {}) or {}
    for name in ("ptx", "sass"):
        body = asm.get(name)
        if not body:
            continue
        if isinstance(body, bytes):
            payload = body
            rendered = body.decode("utf-8", errors="replace")
        else:
            rendered = str(body)
            payload = rendered.encode("utf-8")
        path = asm_root / f"{key}.{name}"
        path.write_bytes(payload)
        result["available"].append(name)
        result["files"][name] = {"path": str(path), "sha256": sha256(payload), "bytes": len(payload)}
        result["instruction_counts"][name] = instruction_counts(rendered, sass=(name == "sass"))
    metadata = getattr(compiled, "metadata", None)
    for key_name in ("num_warps", "num_ctas", "n_regs", "n_spills", "shared", "local", "cluster_dims", "maxnreg"):
        value = getattr(metadata, key_name, None) if metadata is not None else None
        if isinstance(value, (int, float, str, bool)) or value is None:
            result["metadata"][key_name] = value
        elif key_name == "cluster_dims":
            result["metadata"][key_name] = list(value)
    result["metadata"]["kernel_n_regs"] = getattr(compiled, "n_regs", None)
    result["metadata"]["kernel_n_spills"] = getattr(compiled, "n_spills", None)
    result["metadata"]["kernel_metadata_shared"] = getattr(compiled, "metadata", None) and getattr(compiled.metadata, "shared", None)
    return result


def run_case(values_cpu, transition_cpu, mode, block_columns, repeats, asm_root, case_key):
    values = values_cpu.to(device="cuda", non_blocking=False).contiguous()
    transition = transition_cpu.to(device="cuda", dtype=torch.float32).contiguous()
    # Compute the same-mode result once and preserve any mode-specific failure.
    started = time.perf_counter()
    output, compiled = frequency_contract(values, transition, mode,
        block_columns=block_columns, return_kernel=True)
    torch.cuda.synchronize()
    wall_seconds = time.perf_counter()-started
    standalone_timing = timed_cuda(lambda: frequency_contract(values, transition, mode,
        block_columns=block_columns), repeats)
    standalone_timing["first_call_wall_seconds_including_compile"] = wall_seconds
    baseline_timing = timed_cuda(lambda: torch.matmul(transition.T, values), repeats)
    fp64_timing = timed_cuda(lambda: torch.matmul(transition.double().T, values.double()), repeats)

    # The baseline is the receiver's current strict Torch matmul with the same
    # represented FP32 operands and global precision guards.
    baseline = torch.matmul(transition.T, values)
    torch.cuda.synchronize()
    fp64 = torch.matmul(transition.double().T, values.double())
    torch.cuda.synchronize()
    # CPU reference uses only a small identical subset to expose operand loss
    # independently of GPU accumulation.  Retain the exact FP32 inputs.
    small_v = values_cpu[:1, :1, :1].contiguous()
    cpu_ref = cpu_frequency_reference(small_v, transition_cpu, mode)
    cpu_gpu = output[:1, :1, :1].detach().cpu().double()
    return {
        "status": "executed", "shape": list(values.shape), "mode": mode,
        "block_columns": block_columns,
        "timing": standalone_timing,
        "torch_matmul_baseline_timing": baseline_timing,
        "fp64_same_fp32_operands_timing": dict(fp64_timing, conversion_scope="both operands converted to FP64 on each timed call"),
        "wall_seconds_first_call": wall_seconds,
        "against_torch_matmul_baseline": row_metrics(output, baseline),
        "against_fp64_contraction_same_fp32_operands": row_metrics(output, fp64),
        "cpu_represented_product_subset": row_metrics(cpu_gpu, cpu_ref),
        "operand_diagnostics": operand_diagnostics(values, transition, mode),
        "kernel_artifacts": save_kernel_artifacts(compiled, asm_root, case_key),
    }


def main(argv=None):
    args = parser().parse_args(argv)
    if args.repeats < 1:
        parser().error("--repeats must be >= 1")
    if any(x not in BLOCK_COLUMNS for x in args.block_columns):
        parser().error(f"--block-columns entries must be among {BLOCK_COLUMNS}")
    output_path = args.output.expanduser().resolve()
    if output_path.exists():
        parser().error(f"output path already exists: {output_path}")
    global torch, PhaseFrequencyLikelihood, PhaseFrequencyModel
    global cpu_frequency_reference, frequency_contract, operand_diagnostics
    try:
        import torch as torch_module
        torch = torch_module
        from triad_phase_frequency import PhaseFrequencyLikelihood as likelihood, PhaseFrequencyModel as model_type
        from triad_phase_tensor import (cpu_frequency_reference as cpu_reference,
            frequency_contract as contract, operand_diagnostics as operand_report)
    except ImportError as error:
        print(f"ERROR: PyTorch/Triton diagnostic dependencies are unavailable: {error}", file=sys.stderr)
        return 2
    PhaseFrequencyLikelihood, PhaseFrequencyModel = likelihood, model_type
    cpu_frequency_reference, frequency_contract, operand_diagnostics = cpu_reference, contract, operand_report
    if not torch.cuda.is_available():
        print("ERROR: CUDA is unavailable; this diagnostic has no CPU execution fallback.", file=sys.stderr)
        return 2
    if torch.backends.cuda.matmul.allow_tf32 or torch.get_float32_matmul_precision() != "highest":
        print("ERROR: CUDA diagnostic requires global TF32 disabled and Torch float32 precision='highest'.", file=sys.stderr)
        return 2

    # This matches the resident-campaign 512-phase model and obtains the
    # frequency transition from the production model's own constructor.
    model = PhaseFrequencyModel(phase_states=512, relative_phase_states=8,
        nominal_dt_seconds=4., frequency_bound_hz=.02, frequency_std_hz=.003,
        phase_diffusion_rad_per_sqrt_second=.00872665)
    receiver = PhaseFrequencyLikelihood(1, bits=1, taps=2, model=model, device="cpu", dtype=torch.float32)
    transition_cpu = receiver._transition(4.)["frequency"].detach().to(dtype=torch.float32, device="cpu").contiguous()
    frequency, phases = receiver.frequency_states, receiver.phase_states
    quadrature = receiver.quadrature_states
    if (frequency, phases, quadrature) != (43, 512, 24):
        raise RuntimeError(f"model-derived shape differs from requested F43/P512/Q24: {(frequency, phases, quadrature)}")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    asm_root = output_path.parent / f"{output_path.stem}-asm"
    if asm_root.exists():
        print(f"ERROR: assembly directory already exists: {asm_root}", file=sys.stderr)
        return 2
    asm_root.mkdir()
    source_paths = [Path(__file__), TOOLS/"triad_phase_tensor.py", TOOLS/"triad_phase_frequency.py"]
    sources = {p.name: {"path": str(p), "sha256": sha256(p.read_bytes())} for p in source_paths}
    report = {
        "schema": "triad_tensor_frequency_diagnostics_v1", "status": "completed_with_case_results",
        "device": torch.cuda.get_device_name(0), "device_capability": list(torch.cuda.get_device_capability(0)),
        "torch_version": torch.__version__, "cuda_runtime_version": torch.version.cuda,
        "triton_version": None,
        "global_guards": {"allow_tf32": torch.backends.cuda.matmul.allow_tf32,
                           "float32_matmul_precision": torch.get_float32_matmul_precision()},
        "model": {"phase_states": phases, "quadrature_states": quadrature,
                  "frequency_states": frequency, "frequency_bound_hz": model.frequency_bound_hz,
                  "nominal_dt_seconds": model.nominal_dt_seconds,
                  "frequency_std_hz": model.frequency_std_hz,
                  "frequency_correlation_seconds": model.frequency_correlation_seconds,
                  "frequency_model": model.frequency_model,
                  "transition_source": "PhaseFrequencyLikelihood(...)._transition(4.)['frequency']",
                  "transition_shape": list(transition_cpu.shape),
                  "transition_row_sum_max_error": float((transition_cpu.double().sum(-1)-1).abs().max())},
        "inputs": {"candidates": 2, "batches": list(args.batches), "patterns": list(args.patterns),
                   "seed": args.seed, "normalization": "each trial/candidate posterior normalized over QFP in FP64, then cast to FP32",
                   "torch_rng": "CPU Generator with independently SHA-256-derived seed per pattern and batch"},
        "methods": {"baseline": "current torch.matmul(T.T, values), strict global precision guards",
                    "fp64": "torch.matmul(T.double().T, values.double()) on the identical represented FP32 inputs",
                    "cpu_subset": "cpu_frequency_reference on first trial/candidate/quadrature, same represented FP32 operands",
                    "timing": "CUDA events and separate host-loop wall, 10 warmups and configured repeats; in-kernel conversion included, output allocation included in host wall"},
        "source_hashes": sources, "assembly_directory": str(asm_root), "cases": []}

    try:
        import triton
        report["triton_version"] = getattr(triton, "__version__", "unknown")
    except ImportError:
        report["triton_version"] = "unavailable; per-case failures will be recorded"
    pattern_indices = {pattern: index for index, pattern in enumerate(DEFAULT_PATTERNS)}
    for pattern in args.patterns:
        for batch in args.batches:
            seed = derived_seed(args.seed, pattern_indices[pattern], batch)
            values_cpu = make_values(pattern, batch, seed, frequency=frequency, phases=phases,
                                     quadrature=quadrature)
            batch_results = []
            for mode in args.modes:
                for block_columns in args.block_columns:
                    case_key = f"{pattern}-b{batch}-{mode}-bc{block_columns}"
                    base = {"pattern": pattern, "batch": batch, "candidate_count": 2,
                            "seed": seed, "mode": mode, "block_columns": block_columns}
                    try:
                        result = run_case(values_cpu, transition_cpu, mode, block_columns,
                                          args.repeats, asm_root, case_key)
                        base.update(result)
                    except Exception as error:  # retain the failure and continue screening
                        base.update({"status": "failed", "failure_type": type(error).__name__,
                                     "failure": str(error)})
                    batch_results.append(base)
            report["cases"].extend(batch_results)
            report["status"] = "partial_checkpoint"
            output_path.write_text(json.dumps(report, indent=2, allow_nan=False)+"\n", encoding="utf-8")
            del values_cpu
            torch.cuda.empty_cache()
    report["status"] = "completed_with_case_results"
    report["case_counts"] = {"executed": sum(c["status"] == "executed" for c in report["cases"]),
                             "failed": sum(c["status"] == "failed" for c in report["cases"])}
    output_path.write_text(json.dumps(report, indent=2, allow_nan=False)+"\n", encoding="utf-8")
    print(json.dumps({"output": str(output_path), "assembly_directory": str(asm_root),
                      "case_counts": report["case_counts"]}))
    return 0 if not report["case_counts"]["failed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

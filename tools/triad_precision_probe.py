#!/usr/bin/env python3
"""Paired sampled-waveform arithmetic and storage-precision probe.

This deliberately small study scores a fixed public candidate subspace bank
against complete sampled observations. It is a known-window numerical probe,
not StreamingReceiver acquisition or a calibrated receiver. Noise and signal
are generated in the sample domain; observation-quantization modes therefore
re-score the actually quantized samples rather than a Gaussian projection.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import platform
import subprocess
import sys
import time
from typing import Any

import torch

SCHEMA = "triad-sampled-precision-probe-v1"
SPLIT_SALTS = {"development": 0x444556, "calibration": 0x43414C, "holdout": 0x484F4C44}
MODES = ("strict-fp64-reference", "complex64-strict", "real-fp32-strict", "fp16-template-storage",
         "bf16-template-storage", "fp16-observation-storage",
         "bf16-observation-storage", "fp16-real-products",
         "bf16-real-products", "fp16-products-fp32-output-attempt",
         "bf16-products-fp32-output-attempt", "tf32-requested",
         "fp8-e4m3-attempt", "int8-real-products")


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _sync(device: torch.device) -> None:
    if device.type == "cuda":
        torch.cuda.synchronize(device)


def _strict_fp32_policy(device: torch.device) -> str | None:
    """Set/read back strict FP32 controls; return a known override blocker."""
    if device.type != "cuda":
        return None
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    torch.set_float32_matmul_precision("highest")
    if os.environ.get("TORCH_ALLOW_TF32_CUBLAS_OVERRIDE") == "1":
        return "TORCH_ALLOW_TF32_CUBLAS_OVERRIDE=1 can override the strict PyTorch setting"
    return None


def _complex_from_parts(real: torch.Tensor, imag: torch.Tensor) -> torch.Tensor:
    return torch.complex(real, imag)


def _quantize_real(x: torch.Tensor, dtype: torch.dtype) -> torch.Tensor:
    """Represent values in a storage dtype, then expose them as FP32."""
    return x.to(dtype).to(torch.float32)


def _real_decomposed(qh: torch.Tensor, y: torch.Tensor, *, product_dtype: torch.dtype | None = None,
                     fp32_output: bool = False, int8: bool = False,
                     fp8: bool = False) -> torch.Tensor:
    """Compute QᴴY from real planes, reporting no complex GEMM assumptions.

    qh is [candidate*column,sample], y is [sample,batch]. For low-precision
    products each real GEMM accumulates internally in the backend's mode.
    Native low outputs are converted to FP32 before recombination. An explicit
    FP32 GEMM output is attempted separately and has no fallback.
    """
    ar, ai = qh.real.contiguous(), qh.imag.contiguous()
    br, bi = y.real.contiguous(), y.imag.contiguous()
    scale_a = torch.ones((), dtype=torch.float32, device=ar.device)
    scale_b = torch.ones((1, br.shape[1]), dtype=torch.float32, device=br.device)
    if fp8 or int8:
        # A deterministic max scale keeps narrow formats away from overflow.
        # Per-observation scaling does not normalize the score: its inverse is
        # restored after each product. These are GEMM input scales, not a
        # modeled converter gain or receiver AGC.
        max_a = torch.maximum(ar.abs().amax(), ai.abs().amax()).clamp_min(1e-30)
        max_b = torch.maximum(br.abs().amax(dim=0), bi.abs().amax(dim=0)).clamp_min(1e-30)
        target = (min(120, max(1, math.floor(.5 * math.sqrt((2**31-1) / ar.shape[1]))))
                  if int8 else 64.)
        scale_a = (target / max_a).to(torch.float32)
        scale_b = (target / max_b).to(torch.float32)[None, :]
        ar, ai = ar * scale_a, ai * scale_a
        br, bi = br * scale_b, bi * scale_b
    if int8:
        if not hasattr(torch, "_int_mm"):
            raise RuntimeError("this Torch build does not expose torch._int_mm")
        a_r, a_i = ar.round().clamp(-127, 127).to(torch.int8), ai.round().clamp(-127, 127).to(torch.int8)
        b_r, b_i = br.round().clamp(-127, 127).to(torch.int8), bi.round().clamp(-127, 127).to(torch.int8)
        dot = lambda a, b: torch._int_mm(a, b).to(torch.float32)
    elif fp8:
        dtype = getattr(torch, "float8_e4m3fn", None)
        if dtype is None:
            raise RuntimeError("Torch does not expose float8_e4m3fn")
        a_r, a_i = ar.to(dtype), ai.to(dtype)
        b_r, b_i = br.to(dtype), bi.to(dtype)
        dot = lambda a, b: (a @ b).to(torch.float32)
    else:
        if product_dtype is not None:
            ar, ai, br, bi = (v.to(product_dtype) for v in (ar, ai, br, bi))
        a_r, a_i, b_r, b_i = ar, ai, br, bi
        if fp32_output:
            dot = lambda a, b: torch.mm(a, b, out_dtype=torch.float32)
        else:
            dot = lambda a, b: (a @ b).to(torch.float32)
    # ``qh`` is already conjugated by the caller, so these are ordinary
    # complex matrix-product signs rather than another conjugate product.
    result_real = dot(a_r, b_r) - dot(a_i, b_i)
    result_imag = dot(a_r, b_i) + dot(a_i, b_r)
    result = _complex_from_parts(result_real, result_imag)
    if fp8 or int8:
        result = result / (scale_a * scale_b)
    return result


def score(q: torch.Tensor, observations: torch.Tensor, mode: str,
          device: torch.device) -> tuple[torch.Tensor, dict[str, Any]]:
    """Score [B,S] samples using a fixed [C,S,K] orthonormal basis."""
    candidates, samples, columns = q.shape
    details: dict[str, Any] = {"requested_mode": mode, "status": "supported",
        "storage_dtype": "complex128" if mode == "strict-fp64-reference" else None,
        "multiply_mode": None, "inner_accumulator": None,
        "partial_output": None, "tf32_dispatch_verified": False,
        "device": str(device)}
    if mode == "strict-fp64-reference":
        qh = q.to(device="cpu", dtype=torch.complex128).conj().permute(0, 2, 1).reshape(candidates * columns, samples)
        y = observations.to(device="cpu", dtype=torch.complex128).transpose(0, 1).contiguous()
        z = qh @ y
        details.update(storage_dtype="complex128", multiply_mode="FP64 complex multiply",
                      inner_accumulator="FP64", partial_output="FP64 complex")
    elif mode == "complex64-strict":
        blocker = _strict_fp32_policy(device)
        if blocker:
            return torch.empty((observations.shape[0], candidates), dtype=torch.float64), {
                **details, "status": "unsupported", "reason": blocker}
        qh = q.to(device=device, dtype=torch.complex64).conj().permute(0, 2, 1).reshape(candidates * columns, samples)
        y = observations.to(device=device, dtype=torch.complex64).transpose(0, 1).contiguous()
        z = qh @ y
        details.update(storage_dtype="complex64", multiply_mode="strict complex FP32; TF32 disabled",
                      inner_accumulator="FP32 per real product", partial_output="complex64")
    elif mode == "real-fp32-strict" or mode == "tf32-requested":
        if mode == "tf32-requested" and device.type != "cuda":
            return torch.empty((observations.shape[0], candidates), dtype=torch.float64), {
                **details, "status": "unsupported", "reason": "TF32 multiply can only be measured on a CUDA device"}
        if device.type == "cuda":
            if mode == "tf32-requested" and os.environ.get("NVIDIA_TF32_OVERRIDE") == "0":
                return torch.empty((observations.shape[0], candidates), dtype=torch.float64), {
                    **details, "status": "unsupported", "reason": "NVIDIA_TF32_OVERRIDE=0 disables TF32"}
            enabled = mode == "tf32-requested"
            torch.backends.cuda.matmul.allow_tf32 = enabled
            torch.set_float32_matmul_precision("high" if enabled else "highest")
            if not enabled and os.environ.get("TORCH_ALLOW_TF32_CUBLAS_OVERRIDE") == "1":
                return torch.empty((observations.shape[0], candidates), dtype=torch.float64), {
                    **details, "status": "unsupported", "reason": "TORCH_ALLOW_TF32_CUBLAS_OVERRIDE=1 can override strict FP32"}
        qh_c = q.to(device=device, dtype=torch.complex64).conj().permute(0, 2, 1).reshape(candidates * columns, samples)
        y_c = observations.to(device=device, dtype=torch.complex64).transpose(0, 1).contiguous()
        z = _real_decomposed(qh_c, y_c)
        details.update(storage_dtype="FP32 real planes",
                      multiply_mode="TF32 requested; dispatch unverified" if mode == "tf32-requested" else "strict IEEE FP32 real products",
                      inner_accumulator="FP32", partial_output="FP32 real/imag sums")
    elif mode in ("fp16-template-storage", "bf16-template-storage",
                  "fp16-observation-storage", "bf16-observation-storage"):
        blocker = _strict_fp32_policy(device)
        if blocker:
            return torch.empty((observations.shape[0], candidates), dtype=torch.float64), {
                **details, "status": "unsupported", "reason": blocker}
        low = torch.float16 if mode.startswith("fp16") else torch.bfloat16
        qh_c = q.to(device=device, dtype=torch.complex64).conj().permute(0, 2, 1).reshape(candidates * columns, samples)
        y_c = observations.to(device=device, dtype=torch.complex64).transpose(0, 1).contiguous()
        if mode.endswith("template-storage"):
            qh_c = _complex_from_parts(_quantize_real(qh_c.real, low), _quantize_real(qh_c.imag, low))
        else:
            y_c = _complex_from_parts(_quantize_real(y_c.real, low), _quantize_real(y_c.imag, low))
        z = _real_decomposed(qh_c, y_c)
        details.update(storage_dtype=str(low), multiply_mode="strict FP32 after explicit storage quantization",
                      inner_accumulator="FP32", partial_output="FP32 real/imag sums",
                      input_observation_nonlinearly_quantized=mode.endswith("observation-storage"))
    elif mode in ("fp16-real-products", "bf16-real-products",
                  "fp16-products-fp32-output-attempt", "bf16-products-fp32-output-attempt"):
        blocker = _strict_fp32_policy(device)
        if blocker:
            return torch.empty((observations.shape[0], candidates), dtype=torch.float64), {
                **details, "status": "unsupported", "reason": blocker}
        if hasattr(torch.backends.cuda.matmul, "allow_fp16_reduced_precision_reduction"):
            torch.backends.cuda.matmul.allow_fp16_reduced_precision_reduction = False
        if hasattr(torch.backends.cuda.matmul, "allow_bf16_reduced_precision_reduction"):
            torch.backends.cuda.matmul.allow_bf16_reduced_precision_reduction = False
        low = torch.float16 if mode.startswith("fp16") else torch.bfloat16
        qh_c = q.to(device=device, dtype=torch.complex64).conj().permute(0, 2, 1).reshape(candidates * columns, samples)
        y_c = observations.to(device=device, dtype=torch.complex64).transpose(0, 1).contiguous()
        output32 = mode.endswith("fp32-output-attempt")
        if output32 and device.type != "cuda":
            return torch.empty((observations.shape[0], candidates), dtype=torch.float64), {
                **details, "status": "unsupported", "reason": "Torch low-input FP32-output GEMM is probed only on CUDA; CPU API support is absent"}
        try:
            z = _real_decomposed(qh_c, y_c, product_dtype=low, fp32_output=output32)
        except (RuntimeError, TypeError, NotImplementedError) as error:
            if output32:
                return torch.empty((observations.shape[0], candidates), dtype=torch.float64), {
                    **details, "status": "unsupported", "reason": f"actual low-input FP32-output GEMM failed: {error}"}
            raise
        details.update(storage_dtype=str(low), multiply_mode=f"{low} real GEMM inputs",
                      inner_accumulator="FP32 per GEMM (backend documented); framework/runtime to verify",
                      partial_output=("FP32 GEMM output requested; runtime dispatch still requires profiler confirmation" if output32
                                      else f"{low} GEMM output, converted to FP32 before recombining four products"),
                      reduced_precision_accumulation_flags_disabled=True)
    elif mode in ("fp8-e4m3-attempt", "int8-real-products"):
        if device.type != "cuda":
            return torch.empty((observations.shape[0], candidates), dtype=torch.float64), {
                **details, "status": "unsupported", "reason": "requested arithmetic mode requires CUDA hardware"}
        qh_c = q.to(device=device, dtype=torch.complex64).conj().permute(0, 2, 1).reshape(candidates * columns, samples)
        y_c = observations.to(device=device, dtype=torch.complex64).transpose(0, 1).contiguous()
        try:
            z = _real_decomposed(qh_c, y_c, fp8=mode.startswith("fp8"), int8=mode.startswith("int8"))
            details.update(storage_dtype="FP8 E4M3" if mode.startswith("fp8") else "INT8 scaled",
                           multiply_mode="native matmul attempted; dispatch requires profiler/SASS confirmation",
                           inner_accumulator="backend accumulator mode unverified",
                           partial_output="FP32 sums of scaled/dequantized products",
                           scaled_input_maximum=(64 if mode.startswith("fp8") else
                               min(120, max(1, math.floor(.5 * math.sqrt((2**31-1) / samples))))),
                           worst_case_int32_partial_bound=(samples * min(120, max(1, math.floor(.5 * math.sqrt((2**31-1) / samples))))**2
                               if mode.startswith("int8") else None),
                           int32_partial_bound_safe=(samples * min(120, max(1, math.floor(.5 * math.sqrt((2**31-1) / samples))))**2 < 2**31
                               if mode.startswith("int8") else None))
        except (RuntimeError, TypeError, NotImplementedError) as error:
            return torch.empty((observations.shape[0], candidates), dtype=torch.float64), {
                **details, "status": "unsupported", "reason": f"actual selected-device operation failed: {error}"}
    else:
        raise ValueError(f"unknown arithmetic mode: {mode}")
    _sync(device if mode != "strict-fp64-reference" else torch.device("cpu"))
    energy = z.abs().square().reshape(candidates, columns, observations.shape[0]).permute(2, 0, 1).sum(-1)
    return energy.to(device="cpu", dtype=torch.float64), details


def _rank_report(gpu_study, raw_basis: torch.Tensor, reference_ranks: torch.Tensor,
                 relative_tolerance: float) -> dict[str, Any]:
    report = {}
    for name, dtype in (("fp16", torch.float16), ("bf16", torch.bfloat16)):
        real = raw_basis.real.to(dtype).to(torch.float32)
        imag = raw_basis.imag.to(dtype).to(torch.float32)
        quantized = torch.complex(real, imag)
        _, ranks = gpu_study._modified_gram_schmidt(quantized, relative_tolerance)
        differing = (ranks.cpu() != reference_ranks.cpu())
        report[name] = {"rank_differences": int(differing.sum()),
                        "ranks_after_quantized_raw_templates": ranks.cpu().tolist(),
                        "rank_policy": relative_tolerance,
                        "scope": "public raw basis storage quantized then existing reorthogonalized FP32 MGS"}
    return report


def _interval(successes: int, trials: int) -> list[float] | None:
    if trials < 1:
        return None
    try:
        from scipy.stats import beta
        lo = 0. if successes == 0 else float(beta.ppf(.025, successes, trials-successes+1))
        hi = 1. if successes == trials else float(beta.ppf(.975, successes+1, trials-successes))
        return [lo, hi]
    except ImportError:
        return None


def _represented_operands(q: torch.Tensor, observations: torch.Tensor, mode: str,
                         device: torch.device) -> tuple[torch.Tensor, torch.Tensor]:
    """Return the FP64 values represented by a mode's actual GEMM inputs."""
    candidates, samples, columns = q.shape
    qh = q.to(device=device, dtype=torch.complex64 if mode != "strict-fp64-reference" else torch.complex128)
    qh = qh.conj().permute(0, 2, 1).reshape(candidates * columns, samples)
    y = observations.to(device=device,
                        dtype=torch.complex64 if mode != "strict-fp64-reference" else torch.complex128)
    y = y.transpose(0, 1).contiguous()
    low = (torch.float16 if mode.startswith("fp16") else torch.bfloat16)
    if mode.endswith("template-storage") or mode.endswith("real-products") or mode.endswith("fp32-output-attempt"):
        qh = torch.complex(qh.real.to(low).to(torch.float32), qh.imag.to(low).to(torch.float32))
    if mode.endswith("observation-storage") or mode.endswith("real-products") or mode.endswith("fp32-output-attempt"):
        y = torch.complex(y.real.to(low).to(torch.float32), y.imag.to(low).to(torch.float32))
    if mode == "fp8-e4m3-attempt" or mode == "int8-real-products":
        int8 = mode.startswith("int8")
        max_a = torch.maximum(qh.real.abs().amax(), qh.imag.abs().amax()).clamp_min(1e-30)
        max_b = torch.maximum(y.real.abs().amax(dim=0), y.imag.abs().amax(dim=0)).clamp_min(1e-30)
        target = min(120, max(1, math.floor(.5*math.sqrt((2**31-1)/samples))) if int8 else 64)
        scale_a = target/max_a
        scale_b = target/max_b
        dtype = torch.int8 if int8 else getattr(torch, "float8_e4m3fn")
        real_a = (qh.real*scale_a).round().clamp(-127,127).to(dtype).to(torch.float64)/scale_a if int8 else (qh.real*scale_a).to(dtype).to(torch.float64)/scale_a
        imag_a = (qh.imag*scale_a).round().clamp(-127,127).to(dtype).to(torch.float64)/scale_a if int8 else (qh.imag*scale_a).to(dtype).to(torch.float64)/scale_a
        real_b = (y.real*scale_b).round().clamp(-127,127).to(dtype).to(torch.float64)/scale_b if int8 else (y.real*scale_b).to(dtype).to(torch.float64)/scale_b
        imag_b = (y.imag*scale_b).round().clamp(-127,127).to(dtype).to(torch.float64)/scale_b if int8 else (y.imag*scale_b).to(dtype).to(torch.float64)/scale_b
        qh = torch.complex(real_a, imag_a)
        y = torch.complex(real_b, imag_b)
    return qh.to(device="cpu", dtype=torch.complex128), y.to(device="cpu", dtype=torch.complex128)


def _reference_scores(qh: torch.Tensor, y: torch.Tensor, candidates: int,
                      columns: int) -> torch.Tensor:
    """FP64 candidate energies from explicit represented Qᴴ and y matrices."""
    z = qh @ y
    return z.abs().square().reshape(candidates, columns, y.shape[1]).permute(2, 0, 1).sum(-1)


def _git_revision(directory: Path) -> str | None:
    try:
        result = subprocess.run(["git", "rev-parse", "HEAD"], cwd=directory,
                                check=False, capture_output=True, text=True, timeout=2)
    except (OSError, subprocess.TimeoutExpired):
        return None
    revision = result.stdout.strip()
    return revision if result.returncode == 0 and revision else None


def _seed(base: int, split: str, index: int) -> int:
    return (base ^ SPLIT_SALTS[split] ^ ((index + 1) * 0x9E3779B97F4A7C15)) & ((1 << 63) - 1)


def _observations(signal: torch.Tensor, variance: float, trials: int, seed: int,
                  split: str, device: torch.device) -> tuple[torch.Tensor, torch.Tensor]:
    # Independent per-trial CPU FP64 Gaussian noise is replayed identically for
    # every precision mode. H0 and H1 share each trial's n for a paired test.
    null_rows, positive_rows = [], []
    signal = signal.to(device="cpu", dtype=torch.complex128)
    scale = math.sqrt(variance / 2.)
    for index in range(trials):
        generator = torch.Generator(device="cpu").manual_seed(_seed(seed, split, index))
        real = torch.randn(signal.numel(), dtype=torch.float64, generator=generator) * scale
        imag = torch.randn(signal.numel(), dtype=torch.float64, generator=generator) * scale
        noise = torch.complex(real, imag)
        null_rows.append(noise)
        positive_rows.append(noise + signal)
    return torch.stack(null_rows), torch.stack(positive_rows)


def _gamma_gate(gpu_study, rank_bound: int, family: int, alpha: float) -> tuple[float, dict[str, Any]]:
    threshold, method, reason = gpu_study._gamma_threshold(rank_bound, math.log(family) - math.log(alpha), "exact")
    return threshold, {"method": method, "fallback_reason": reason,
                       "scope": "analytic Bonferroni fixed-rank white-Gaussian threshold; invalid for quantized observations"}


def evaluate(args: argparse.Namespace) -> dict[str, Any]:
    job_started = time.perf_counter()
    root = Path(__file__).resolve().parents[1]
    load_started = time.perf_counter()
    gpu_study = load_module("triad_gpu_precision_probe_base", root / "tools/triad-gpu-study.py")
    directory, manifest = gpu_study._load_manifest(args.input)
    data = gpu_study._validate_manifest(directory, manifest)
    candidates, samples, columns = data["basis_shape"]
    if data["true_candidate_index"] is None and args.truth_candidate is None:
        raise ValueError("input must declare true_candidate_index or --truth-candidate")
    truth = data["true_candidate_index"] if args.truth_candidate is None else args.truth_candidate
    if not 0 <= truth < candidates:
        raise ValueError("truth candidate is outside public bank")
    if data["signal_bit"] is None:
        raise ValueError("input must declare signal_bit to score source-bit correctness")
    if not math.isfinite(args.rank_tolerance) or not 0 < args.rank_tolerance < .01:
        raise ValueError("rank tolerance must be finite in (0,.01)")
    if not 0 < args.false_probability < 1:
        raise ValueError("false probability must lie in (0,1)")
    if (not math.isfinite(args.horizon_seconds) or args.horizon_seconds <= 0 or
            not math.isfinite(args.attempt_spacing) or args.attempt_spacing <= 0 or
            isinstance(args.duration_alternatives, bool) or args.duration_alternatives < 1):
        raise ValueError("operational family spacing/count must be positive")
    device = torch.device(args.device)
    if device.type == "cuda" and (not torch.cuda.is_available() or device.index is not None and device.index >= torch.cuda.device_count()):
        raise ValueError("requested CUDA device is unavailable")
    if device.type not in ("cpu", "cuda"):
        raise ValueError("device must be cpu or cuda")
    if device.type == "cuda":
        torch.cuda.set_device(device)
    torch.set_num_threads(args.threads)
    if (args.trials < 1 or args.trials > 100000 or args.threads < 1 or
            args.warmup < 0 or args.warmup > 100):
        raise ValueError("trials, threads or warmup outside bounded limits")
    if not args.cn0_grid or any(not math.isfinite(value) for value in args.cn0_grid):
        raise ValueError("C/N0 grid must contain finite values")
    if not math.isfinite(args.memory_gib) or not 0 < args.memory_gib <= 1024:
        raise ValueError("memory-gib must be finite and in (0,1024]")
    # Lists, stacked FP64 samples, selected-device FP32 copies, projection
    # workspaces and FP64 templates are all live together in parts of this
    # probe. Reject before converting the public basis or stacking trials.
    estimated_peak_bytes = 128*args.trials*samples + 96*candidates*samples*columns
    memory_limit_bytes = int(args.memory_gib * 1024**3)
    if estimated_peak_bytes > memory_limit_bytes:
        raise ValueError(f"estimated working set {estimated_peak_bytes} bytes exceeds --memory-gib cap {memory_limit_bytes}; reduce trials or use a higher explicit cap")
    load_validate_seconds = time.perf_counter() - load_started
    materialize_start = time.perf_counter()
    basis = data["basis"].to(device="cpu", dtype=torch.complex128)
    signal = data["signal"].to(device="cpu", dtype=torch.complex128)
    input_materialization_seconds = time.perf_counter() - materialize_start
    if not args.modes or any(mode not in MODES for mode in args.modes):
        raise ValueError(f"modes must be selected from {', '.join(MODES)}")
    if len(set(args.modes)) != len(args.modes):
        raise ValueError("precision modes must be distinct")

    prep_start = time.perf_counter()
    q64, ranks64 = gpu_study._modified_gram_schmidt(basis, args.rank_tolerance)
    q64 = q64.to(torch.complex128)
    prep_seconds = time.perf_counter() - prep_start
    rank_start = time.perf_counter()
    rank_report = _rank_report(gpu_study, basis.to(torch.complex64), ranks64, args.rank_tolerance)
    rank_seconds = time.perf_counter() - rank_start
    variance_values = []
    for cn0 in args.cn0_grid:
        if not math.isfinite(cn0) or not -300 < cn0 / 10 < 300:
            raise ValueError("C/N0 grid is outside representable range")
        variance_values.append((cn0, data["noise_reference_power"] * data["sample_rate"] / 10. ** (cn0 / 10.)))
    attempts = math.ceil(args.horizon_seconds / args.attempt_spacing)
    # This probe exercises one known sample window. The declared family budgets
    # candidate tests across schedule attempts and selected duration choices;
    # no timing/epoch/FHSS acquisition is implemented here.
    family = candidates * attempts * args.duration_alternatives
    if args.family_hypotheses is not None:
        if args.family_hypotheses < family:
            raise ValueError("family-hypotheses may not omit the declared partial search family")
        family = args.family_hypotheses
    alpha = args.false_probability
    gate, gate_details = _gamma_gate(gpu_study, min(samples, columns), family, alpha)
    results: list[dict[str, Any]] = []
    mode_details: dict[str, Any] = {}
    total_rng = total_transfer = total_infer = total_verify = 0.
    total_template_prepare = total_backend_probe = total_warmup = 0.
    for cn0, variance in variance_values:
        rng_start = time.perf_counter()
        null, positive = _observations(signal, variance, args.trials, args.seed, args.split, device)
        rng_seconds = time.perf_counter() - rng_start
        total_rng += rng_seconds
        q_original_h = q64.conj().permute(0, 2, 1).reshape(candidates * columns, samples)
        original_ref0 = _reference_scores(q_original_h, null.T.contiguous(), candidates, columns)
        original_ref1 = _reference_scores(q_original_h, positive.T.contiguous(), candidates, columns)
        for mode in args.modes:
            selected_device = torch.device("cpu") if mode == "strict-fp64-reference" else device
            template_start = time.perf_counter()
            if mode == "strict-fp64-reference":
                q_work = q64
            else:
                q_work = q64.to(device=selected_device, dtype=torch.complex64)
            if mode == "fp16-template-storage":
                q_work = torch.complex(q_work.real.half().float(), q_work.imag.half().float())
            elif mode == "bf16-template-storage":
                q_work = torch.complex(q_work.real.bfloat16().float(), q_work.imag.bfloat16().float())
            _sync(selected_device)
            template_prepare_seconds = time.perf_counter() - template_start
            total_template_prepare += template_prepare_seconds
            if mode not in mode_details:
                # Probe the selected implementation with actual dimensions
                # before timing; retain setup/probe time separately.
                # Eligibility can depend on the output column count (notably
                # INT8/FP8). Probe the measured shape, not a batch-one proxy.
                dummy = torch.zeros((args.trials, samples), dtype=torch.complex128, device=selected_device)
                probe_start = time.perf_counter()
                _, detail = score(q_work, dummy, mode, selected_device)
                _sync(selected_device)
                detail["backend_probe_seconds"] = time.perf_counter() - probe_start
                total_backend_probe += detail["backend_probe_seconds"]
                mode_details[mode] = detail
            detail = mode_details[mode]
            if detail["status"] != "supported":
                results.append({"cn0_db_hz": cn0, "mode": mode, "status": "unsupported",
                                "reason": detail.get("reason")})
                continue
            # Make two explicit transfers and count their synchronized wall
            # time separately from resident projection and output collection.
            transfer_start = time.perf_counter()
            if mode == "strict-fp64-reference":
                y0 = null
                y1 = positive
            else:
                y0 = null.to(device=device, dtype=torch.complex64)
                y1 = positive.to(device=device, dtype=torch.complex64)
            _sync(selected_device)
            transfer_seconds = time.perf_counter() - transfer_start
            total_transfer += transfer_seconds
            # Warmup on these fixed shapes, included in separately reported
            # preparation but excluded from measured inference.
            warmup_start = time.perf_counter()
            for _ in range(args.warmup):
                score(q_work, y0, mode, selected_device)
            _sync(selected_device)
            warmup_seconds = time.perf_counter() - warmup_start
            total_warmup += warmup_seconds
            infer_start = time.perf_counter()
            event_start = event_stop = None
            if selected_device.type == "cuda":
                event_start = torch.cuda.Event(enable_timing=True)
                event_stop = torch.cuda.Event(enable_timing=True)
                event_start.record(torch.cuda.current_stream(selected_device))
            e0, detail = score(q_work, y0, mode, selected_device)
            e1, detail = score(q_work, y1, mode, selected_device)
            if event_stop is not None:
                event_stop.record(torch.cuda.current_stream(selected_device))
                event_stop.synchronize()
            _sync(selected_device)
            infer_seconds = time.perf_counter() - infer_start
            event_seconds = event_start.elapsed_time(event_stop) / 1000. if event_start is not None else None
            total_infer += infer_seconds
            mode_details[mode] = detail
            if detail["status"] != "supported":
                results.append({"cn0_db_hz": cn0, "mode": mode, "status": "unsupported", "reason": detail.get("reason")})
                continue
            verify_start = time.perf_counter()
            # Independent reference uses CPU FP64 arithmetic on the same
            # already generated physical samples. For observation-storage
            # modes, recompute with their actual quantized sample values.
            represented_qh0, represented_y0 = _represented_operands(q_work, y0, mode, selected_device)
            represented_qh1, represented_y1 = _represented_operands(q_work, y1, mode, selected_device)
            same_ref0 = _reference_scores(represented_qh0, represented_y0, candidates, columns)
            same_ref1 = _reference_scores(represented_qh1, represented_y1, candidates, columns)
            # Row-wise family test: max over all fixed candidate hypotheses,
            # using the common column-count rank upper bound.
            predicted0 = torch.as_tensor(data["bitlabels"])[e0.argmax(1)]
            predicted1 = torch.as_tensor(data["bitlabels"])[e1.argmax(1)]
            admits0 = e0.max(1).values / variance >= gate
            admits1 = e1.max(1).values / variance >= gate
            correct = admits1 & (predicted1 == int(data["signal_bit"]))
            wrong = admits1 & (predicted1 != int(data["signal_bit"]))
            ranked = e1.argsort(dim=1, descending=True)
            best = e1.gather(1, ranked[:, :1]).squeeze(1) / variance
            runner_up = e1.gather(1, ranked[:, 1:2]).squeeze(1) / variance
            reference_ranked = original_ref1.argsort(dim=1, descending=True)
            reference_best = original_ref1.gather(1, reference_ranked[:, :1]).squeeze(1) / variance
            reference_runner_up = original_ref1.gather(1, reference_ranked[:, 1:2]).squeeze(1) / variance
            reference_margin = reference_best - reference_runner_up
            near_wrong_candidate_boundary = reference_margin <= .05 * reference_best.abs().clamp_min(1.)
            truth_first = e1.argmax(1) == truth
            gate_valid = mode == "strict-fp64-reference"
            metrics = {"trials": args.trials,
                "diagnostic_counts_at_hypothetical_threshold": {
                    "correct_bit_accepted": int(correct.sum()), "wrong_bit_accepted": int(wrong.sum()),
                    "missed": int((~correct & ~wrong).sum()), "null_gate_passes": int(admits0.sum())},
                "diagnostic_correct_reception_95pct_ci": _interval(int(correct.sum()), args.trials),
                "diagnostic_null_pass_95pct_ci": _interval(int(admits0.sum()), args.trials),
                "calibrated_counts": {"correct": None, "wrong": None, "missed": None, "null": None},
                "gate_validity": {"status": "analytic_partial_family_only" if gate_valid else "invalid_for_this_precision_mode",
                    "operationally_calibrated": False,
                    "reason": ("fixed known-alignment white-Gaussian subfamily only; complete timing/CFO/epoch/method schedule is not exercised or empirically calibrated"
                               if gate_valid else "precision/input representation changes the statistic distribution or strict reference arithmetic; no calibration holdout is supplied")},
                "ranking_differences_from_fp64_same_represented_inputs": int((e1.argmax(1) != same_ref1.argmax(1)).sum()),
                "ranking_changes_vs_unquantized_input_reference": int((e1.argmax(1) != original_ref1.argmax(1)).sum()),
                "admission_differences_vs_unquantized_input_reference_at_hypothetical_gate": int((admits1 != (original_ref1.max(1).values / variance >= gate)).sum()),
                "near_wrong_candidate_boundary_trials": int(near_wrong_candidate_boundary.sum()),
                "near_boundary_candidate_rank_changes": int(((e1.argmax(1) != original_ref1.argmax(1)) & near_wrong_candidate_boundary).sum()),
                "minimum_reference_candidate_margin_over_noise_trials": float(reference_margin.min()),
                "minimum_candidate_margin_over_noise_trials": float((best - runner_up).min()),
                "truth_candidate_ranked_first": int(truth_first.sum()),
                "truth_candidate_ranked_first_95pct_ci": _interval(int(truth_first.sum()), args.trials),
                "maximum_arithmetic_score_error_same_represented_inputs": float(torch.maximum((e0-same_ref0).abs().max(), (e1-same_ref1).abs().max())),
                "mean_arithmetic_score_error_same_represented_inputs": float(((e0-same_ref0).abs().mean() + (e1-same_ref1).abs().mean())/2),
                "maximum_representation_score_change_vs_original_inputs": float(torch.maximum((same_ref0-original_ref0).abs().max(), (same_ref1-original_ref1).abs().max())),
                "maximum_total_score_error_vs_original_fp64_input_reference": float(torch.maximum((e0-original_ref0).abs().max(), (e1-original_ref1).abs().max())),
                "precision_gate": gate, "familial_false_probability": alpha,
                "sensitivity_evidence": "screening point only; independent path campaign and threshold curve required"}
            verify_seconds = time.perf_counter() - verify_start
            total_verify += verify_seconds
            results.append({"cn0_db_hz": cn0, "noise_variance": variance,
                "mode": mode, "status": "screening_complete", "metrics": metrics,
                "timing": {"transfer_wall_seconds": transfer_seconds,
                           "paired_batch_inference_wall_seconds": infer_seconds,
                           "paired_score_hypotheses_per_second": (2*args.trials/infer_seconds if infer_seconds else None),
                           "cuda_event_score_path_seconds": event_seconds,
                           "template_transfer_and_encoding_seconds": template_prepare_seconds,
                           "backend_probe_seconds": mode_details[mode].get("backend_probe_seconds", 0.),
                           "warmup_seconds": warmup_seconds,
                           "verification_wall_seconds": verify_seconds,
                           "score_call_timing_scope": "synchronized score calls; includes layout/conversion and output materialization/device-to-host collection; CUDA events report device-side score path only when available; excludes input generation/transfer/FP64 verification"}})
    complete_evaluate_seconds = time.perf_counter() - job_started
    measured_substage_sum = (load_validate_seconds + input_materialization_seconds + prep_seconds + rank_seconds + total_rng +
        total_template_prepare + total_backend_probe + total_transfer + total_warmup + total_infer + total_verify)
    return {"schema": SCHEMA, "status": "screening_only", "study_scope": "known aligned fixed sampled subspace bank; not full acquisition or StreamingReceiver",
        "source": {"git_revision": _git_revision(root),
            "command_argv": sys.argv,
            "script_sha256": sha256(Path(__file__).resolve()),
            "gpu_study_script_sha256": sha256(root / "tools/triad-gpu-study.py"),
            "input_hashes": {name: sha256(directory / name) for name in (manifest.get("basis_file", "basis.c64"), manifest.get("signal_file", "signal.c64"), "manifest.json")}},
        "environment": {"python": sys.version, "torch": torch.__version__, "numpy": __import__("numpy").__version__,
            "scipy": __import__("scipy").__version__, "platform": platform.platform(),
            "device": str(device), "device_name": torch.cuda.get_device_name(device) if device.type == "cuda" else platform.processor() or "CPU",
            "cuda_available": torch.cuda.is_available(), "cuda_runtime": torch.version.cuda,
            "tf32_flag_after_modes": bool(torch.backends.cuda.matmul.allow_tf32),
            "float32_matmul_precision": torch.get_float32_matmul_precision(),
            "fp16_reduced_precision_reduction": getattr(torch.backends.cuda.matmul, "allow_fp16_reduced_precision_reduction", None),
            "bf16_reduced_precision_reduction": getattr(torch.backends.cuda.matmul, "allow_bf16_reduced_precision_reduction", None),
            "TORCH_ALLOW_TF32_CUBLAS_OVERRIDE": os.environ.get("TORCH_ALLOW_TF32_CUBLAS_OVERRIDE"),
            "NVIDIA_TF32_OVERRIDE": os.environ.get("NVIDIA_TF32_OVERRIDE")},
        "inputs": {"manifest_format": manifest.get("format"), "sample_rate": data["sample_rate"],
            "sample_count": samples, "duration_seconds": samples/data["sample_rate"],
            "candidate_count": candidates, "candidate_columns": columns,
            "true_candidate_index": truth, "signal_bit": data["signal_bit"],
            "bitlabels": data["bitlabels"], "noise_reference_power": data["noise_reference_power"],
            "rank_relative_tolerance": args.rank_tolerance},
        "split": args.split, "root_seed": args.seed,
        "derived_noise_seed_recipe": {"split_salt": SPLIT_SALTS[args.split],
            "trial_index_range_inclusive": [0, args.trials-1],
            "formula": "(root_seed XOR split_salt XOR ((trial_index+1)*0x9E3779B97F4A7C15)) mod 2^63"},
        "seed_rule": "per-trial CPU FP64 complex Gaussian samples; H0/H1 paired by adding the same signal to the same noise; split salted",
        "timing": {"load_and_manifest_validation_seconds": load_validate_seconds,
            "input_sample_materialization_seconds": input_materialization_seconds,
            "template_projector_preparation_seconds": prep_seconds,
            "quantized_rank_diagnostic_seconds": rank_seconds,
            "paired_sample_noise_generation_seconds_sum_over_cn0": total_rng,
            "template_transfer_and_encoding_seconds_sum": total_template_prepare,
            "backend_probe_warmup_seconds_sum": total_backend_probe,
            "host_device_transfer_seconds_sum": total_transfer,
            "warmup_seconds_sum": total_warmup,
            "inference_seconds_sum": total_infer, "verification_seconds_sum": total_verify,
            "sum_of_measured_substages_seconds": measured_substage_sum,
            "unattributed_or_overlapping_evaluate_wall_seconds": max(0., complete_evaluate_seconds-measured_substage_sum),
            "complete_evaluate_wall_seconds": complete_evaluate_seconds,
            "complete_process_startup_and_serialization_seconds": None,
            "complete_process_note": "evaluate wall includes manifest/basis load, preparation, all CN0 generation and every requested mode; Python/Torch startup, JSON serialization and file write occur outside evaluate"},
        "reference": {"arithmetic": "complex128 CPU MGS and complex projection on exported complex64 samples",
            "waveform_independence": "not an independent waveform generator; FP64 only increases arithmetic precision on the exact hashed complex64 exported inputs",
            "reference_ranks": ranks64.cpu().tolist(), "quantized_template_rank_diagnostics": rank_report},
        "working_set": {"estimated_peak_bytes": estimated_peak_bytes,
            "estimate_scope": "conservative host/device arrays for trials, C128 basis/projectors and selected-device samples; runtime/library allocator overhead remains outside this bound",
            "memory_cap_bytes": memory_limit_bytes},
        "receiver_statistics": {"family_hypotheses": family, "candidate_hypotheses_per_capture": candidates,
            "schedule_attempts": attempts, "duration_alternatives": args.duration_alternatives,
            "horizon_seconds": args.horizon_seconds, "attempt_spacing_seconds": args.attempt_spacing,
            "false_probability_target": alpha, "threshold": gate, "threshold_details": gate_details,
            "limitations": ["single aligned known-window family: no timing/CFO/epoch search", "known white Gaussian null only; observation quantization invalidates analytic threshold", "small screening trial counts do not establish any daily tail or general path reliability"]},
        "requested_modes": list(args.modes), "mode_details": mode_details, "results": results}


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("input", type=Path, help="triad-long-basis-v1 export directory or manifest")
    p.add_argument("--device", default="cpu", help="cpu, cuda, or cuda:N")
    p.add_argument("--modes", default="strict-fp64-reference,complex64-strict,real-fp32-strict,fp16-template-storage,bf16-template-storage,fp16-observation-storage,bf16-observation-storage,fp16-real-products,bf16-real-products,fp16-products-fp32-output-attempt,bf16-products-fp32-output-attempt,tf32-requested,fp8-e4m3-attempt,int8-real-products")
    p.add_argument("--trials", type=int, default=64)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--split", choices=tuple(SPLIT_SALTS), default="development")
    p.add_argument("--cn0-grid", default=None, help="comma-separated dB-Hz sweep; defaults to the manifest C/N0")
    p.add_argument("--truth-candidate", type=int, default=None)
    p.add_argument("--rank-tolerance", type=float, default=1e-6)
    p.add_argument("--false-probability", type=float, default=1e-4)
    p.add_argument("--horizon-seconds", type=float, default=86400.)
    p.add_argument("--attempt-spacing", type=float, default=60.)
    p.add_argument("--duration-alternatives", type=int, default=1)
    p.add_argument("--family-hypotheses", type=int, default=None)
    p.add_argument("--threads", type=int, default=2)
    p.add_argument("--warmup", type=int, default=1)
    p.add_argument("--memory-gib", type=float, default=2.0,
                   help="conservative working-set cap checked before allocating trial stacks")
    p.add_argument("--output", type=Path, default=None)
    return p


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    args.modes = [part.strip() for part in args.modes.split(",") if part.strip()]
    if args.cn0_grid is None:
        _, manifest = load_module("triad_gpu_probe_loader", Path(__file__).resolve().with_name("triad-gpu-study.py"))._load_manifest(args.input)
        args.cn0_grid = [float(manifest.get("cn0_db_hz", 0.))]
    else:
        args.cn0_grid = [float(value) for value in args.cn0_grid.split(",")]
    try:
        result = evaluate(args)
    except (ValueError, OSError, RuntimeError, ImportError) as error:
        print(json.dumps({"schema": SCHEMA, "status": "error", "reason": str(error)}), file=sys.stderr)
        return 2
    rendered = json.dumps(result, sort_keys=True, separators=(",", ":"))
    if args.output is None:
        print(rendered)
    else:
        if args.output.exists():
            print(f"output already exists: {args.output}", file=sys.stderr)
            return 2
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

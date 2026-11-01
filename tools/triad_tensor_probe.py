#!/usr/bin/env python3
"""Explicitly padded FP8/INT8 sample-domain projection experiments.

No arithmetic fallback is permitted. Padding adds zeros outside the original
dot product. Results are numerical screening of a fixed known-window family;
quantized observations have no Gaussian admission calibration in this probe.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import math
from pathlib import Path
import platform
import sys
import time

import torch

import triad_precision_probe as base

MODES = ("fp8-e4m3-scaled", "fp8-e5m2-scaled", "int8-padded")


def round_up(value: int, multiple: int) -> int:
    return ((value+multiple-1)//multiple)*multiple


@dataclass
class EncodedProduct:
    ar: torch.Tensor
    ai: torch.Tensor
    br: torch.Tensor
    bi: torch.Tensor
    scale_a: torch.Tensor
    scale_b: torch.Tensor
    original_shape: tuple[int, int, int]
    mode: str
    target: float

    def represented(self) -> tuple[torch.Tensor, torch.Tensor]:
        """Independent CPU FP64 values actually represented by encoded inputs."""
        m, k, n = self.original_shape
        sa = self.scale_a.cpu().double()
        sb = self.scale_b[:, :n].cpu().double()
        a = torch.complex(self.ar[:m, :k].cpu().double(), self.ai[:m, :k].cpu().double())/sa
        b = torch.complex(self.br[:k, :n].cpu().double(), self.bi[:k, :n].cpu().double())/sb
        return a, b

    def details(self) -> dict:
        m, k, n = self.original_shape
        return {"mode": self.mode, "original_real_gemm_shape": [m, k, n],
                "padded_real_gemm_shape": [self.ar.shape[0], self.ar.shape[1], self.br.shape[1]],
                "input_dtype": str(self.ar.dtype), "a_stride": list(self.ar.stride()),
                "b_stride": list(self.br.stride()), "scaled_input_maximum_target": self.target,
                "scaling": "one template scale shared by both real planes; per observation-column scale shared by both planes; exact inverse restored after GEMM",
                "int32_absolute_partial_bound": k*int(self.target)**2 if self.mode == "int8-padded" else None,
                "accumulator_contract": "INT32 operator result" if self.mode == "int8-padded" else "torch._scaled_mm FP32 output; use_fast_accum=False",
                "recombination": "four product outputs converted to FP64 before complex addition/subtraction and inverse scaling",
                "dispatch_verified": False,
                "dispatch_limit": "successful requested native operator and output dtype are checked; tensor instructions/internal accumulation require profiler or SASS evidence"}


def encode(a: torch.Tensor, b: torch.Tensor, mode: str) -> EncodedProduct:
    """Encode complex [M,K] and [K,N] operands; usable on CPU for inspection."""
    if mode not in MODES or a.ndim != 2 or b.ndim != 2 or a.shape[1] != b.shape[0]:
        raise ValueError("known mode and compatible two-dimensional matrices required")
    if not a.is_complex() or not b.is_complex() or a.device != b.device or min(*a.shape, *b.shape) < 1:
        raise ValueError("nonempty complex operands on one device required")
    if not bool(torch.isfinite(a).all()) or not bool(torch.isfinite(b).all()):
        raise ValueError("finite operands required")
    m, k, n = a.shape[0], a.shape[1], b.shape[1]
    mp, kp, npad = max(32, round_up(m, 32)), round_up(k, 32), round_up(n, 16)
    # INT32 safety covers every possible partial dot product. A factor of two
    # margin is retained; real/imag products are recombined only after FP64 cast.
    target = min(120, max(1, math.floor(math.sqrt((2**31-1)/(2*k))))) if mode == "int8-padded" else 64.
    if mode == "int8-padded" and k*target**2 >= 2**31:
        raise ValueError("sample length cannot satisfy the INT32 bound")
    dtype = torch.int8 if mode == "int8-padded" else getattr(torch, "float8_e4m3fn" if "e4m3" in mode else "float8_e5m2", None)
    if dtype is None:
        raise RuntimeError("requested FP8 dtype absent from this Torch build")
    a, b = a.to(torch.complex64), b.to(torch.complex64)
    maxa = torch.maximum(a.real.abs().amax(), a.imag.abs().amax()).clamp_min(1e-30)
    maxb = torch.maximum(b.real.abs().amax(0), b.imag.abs().amax(0)).clamp_min(1e-30)
    sa = (target/maxa).float()
    sb = torch.ones((1, npad), dtype=torch.float32, device=b.device)
    sb[:, :n] = target/maxb

    def plane(x, scale, rows, cols, column_major=False):
        scaled = x*scale
        if mode == "int8-padded":
            scaled = scaled.round().clamp(-127, 127)
        result = torch.zeros((rows, cols), dtype=dtype, device=x.device)
        result[:x.shape[0], :x.shape[1]] = scaled.to(dtype)
        return result.T.contiguous().T if column_major else result

    column_major = mode != "int8-padded"
    return EncodedProduct(plane(a.real, sa, mp, kp), plane(a.imag, sa, mp, kp),
                          plane(b.real, sb[:, :n], kp, npad, column_major),
                          plane(b.imag, sb[:, :n], kp, npad, column_major),
                          sa, sb, (m, k, n), mode, float(target))


def project(encoded: EncodedProduct) -> torch.Tensor:
    """Execute the requested native operator, returning [M,N] complex128."""
    if encoded.ar.device.type != "cuda":
        raise RuntimeError("native padded tensor probe requires CUDA; CPU fallback is prohibited")
    if encoded.mode == "int8-padded":
        if not hasattr(torch, "_int_mm"):
            raise RuntimeError("torch._int_mm unavailable")

        def dot(a, b):
            value = torch._int_mm(a, b)
            if value.dtype != torch.int32:
                raise RuntimeError(f"requested INT32 product returned {value.dtype}")
            return value.double()
    else:
        if not hasattr(torch, "_scaled_mm"):
            raise RuntimeError("torch._scaled_mm unavailable")
        one = torch.ones((), dtype=torch.float32, device=encoded.ar.device)

        def dot(a, b):
            value = torch._scaled_mm(a, b, scale_a=one, scale_b=one,
                                     out_dtype=torch.float32, use_fast_accum=False)
            if not isinstance(value, torch.Tensor) or value.dtype != torch.float32:
                raise RuntimeError("_scaled_mm did not return the requested FP32 tensor")
            return value.double()
    real = dot(encoded.ar, encoded.br)-dot(encoded.ai, encoded.bi)
    imag = dot(encoded.ar, encoded.bi)+dot(encoded.ai, encoded.br)
    m, _, n = encoded.original_shape
    return (torch.complex(real, imag)/(encoded.scale_a.double()*encoded.scale_b.double()))[:m, :n]


def inventory() -> dict:
    def schema(name):
        try:
            return str(getattr(torch.ops.aten, name).default._schema)
        except (AttributeError, RuntimeError):
            return None
    return {"torch": torch.__version__, "cuda_build": torch.version.cuda,
            "python": platform.python_version(), "scaled_mm_schema": schema("_scaled_mm"),
            "int_mm_schema": schema("_int_mm"),
            "low_dtype_attributes": [x for x in dir(torch) if "float4" in x or "float8" in x],
            "fp4_block_scaled_status": "not implemented or exercised by this probe; dtype/API inventory alone does not establish usable packed FP4 or block-scaled CUDA arithmetic",
            "hardware_counters": "not collected by this probe"}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("input", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--device", default="cuda:0")
    p.add_argument("--trials", type=int, default=64)
    p.add_argument("--seed", type=int, default=194951)
    p.add_argument("--split", choices=tuple(base.SPLIT_SALTS), default="development")
    p.add_argument("--cn0-grid", default="0,1,2")
    p.add_argument("--modes", default=",".join(MODES))
    p.add_argument("--repeats", type=int, default=3)
    p.add_argument("--memory-gib", type=float, default=4.)
    a = p.parse_args()
    cn0s = [float(x) for x in a.cn0_grid.split(",")]
    modes = a.modes.split(",")
    if (a.output.exists() or not 1 <= a.trials <= 4096 or not 1 <= a.repeats <= 30 or
            not cn0s or any(not math.isfinite(x) or abs(x) > 200 for x in cn0s) or
            not modes or len(set(modes)) != len(modes) or any(x not in MODES for x in modes) or
            not math.isfinite(a.memory_gib) or a.memory_gib <= 0):
        p.error("fresh output, valid modes, bounded trials/repeats/CN0 and positive memory required")
    start = time.perf_counter()
    torch.set_num_threads(2)
    device = torch.device(a.device)
    if device.type not in ("cpu", "cuda"):
        p.error("device must be cpu or cuda")
    if device.type == "cuda":
        torch.cuda.set_device(device)
    base._strict_fp32_policy(device)
    gpu = base.load_module("triad_tensor_bank", Path(__file__).with_name("triad-gpu-study.py"))
    directory, manifest = gpu._load_manifest(a.input)
    data = gpu._validate_manifest(directory, manifest)
    c, samples, columns = data["basis_shape"]
    estimate = samples*a.trials*2*16*12 + c*samples*columns*16*8
    if estimate > a.memory_gib*1024**3:
        p.error(f"estimated host/device/reference arrays {estimate} bytes exceed --memory-gib")
    raw = torch.as_tensor(data["basis"]).clone().to(torch.complex128)
    q, ranks = gpu._modified_gram_schmidt(raw, 1e-6)
    qh = q.conj().permute(0, 2, 1).reshape(c*columns, samples)
    signal = torch.as_tensor(data["signal"]).clone().to(torch.complex128)
    labels = torch.tensor(data["bitlabels"])
    truth = data["signal_bit"]
    if truth is None:
        p.error("signal_bit is required")
    attempts = 1440
    family = c*attempts
    gate, gate_meta = base._gamma_gate(gpu, columns, family, 1e-4)
    rows = []
    for cn0 in cn0s:
        variance = data["noise_reference_power"]*data["sample_rate"]/10**(cn0/10)
        rng_start = time.perf_counter()
        null, signal_obs = base._observations(signal, variance, a.trials, a.seed, a.split, torch.device("cpu"))
        y = torch.cat((null, signal_obs)).T.contiguous()
        rng_seconds = time.perf_counter()-rng_start
        reference_start = time.perf_counter()
        ref = base._reference_scores(qh, y, c, columns)
        reference_seconds = time.perf_counter()-reference_start
        for mode in modes:
            row = {"cn0_db_hz": cn0, "mode": mode, "status": "unsupported",
                   "timing_seconds": {"sample_noise_generation": rng_seconds,
                                      "unquantized_cpu_fp64_reference": reference_seconds}}
            try:
                prep_start = time.perf_counter()
                encoded = encode(qh.to(device), y.to(device), mode)
                base._sync(device)
                row["timing_seconds"]["transfer_scale_quantize_pad"] = time.perf_counter()-prep_start
                row["operator"] = encoded.details()
                warm_start = time.perf_counter()
                project(encoded)
                base._sync(device)
                row["timing_seconds"]["warmup_actual_full_shape"] = time.perf_counter()-warm_start
                times, events = [], []
                for _ in range(a.repeats):
                    first, last = torch.cuda.Event(enable_timing=True), torch.cuda.Event(enable_timing=True)
                    base._sync(device)
                    t = time.perf_counter()
                    first.record()
                    z = project(encoded)
                    last.record()
                    last.synchronize()
                    times.append(time.perf_counter()-t)
                    events.append(first.elapsed_time(last)/1000)
                row["timing_seconds"].update(resident_padded_projection_wall_samples=times,
                                               resident_padded_projection_cuda_event_samples=events)
                verify_start = time.perf_counter()
                actual = z.cpu().abs().square().reshape(c, columns, 2*a.trials).permute(2, 0, 1).sum(-1)
                ra, rb = encoded.represented()
                represented = base._reference_scores(ra, rb, c, columns)
                predicted = labels[actual.argmax(1)]
                admission = actual.max(1).values/variance >= gate
                positive_admission, positive_prediction = admission[a.trials:], predicted[a.trials:]
                row.update(status="screening_complete", metrics={
                    "max_arithmetic_score_error_same_represented_inputs": float((actual-represented).abs().max()),
                    "max_representation_score_change": float((represented-ref).abs().max()),
                    "max_total_score_error": float((actual-ref).abs().max()),
                    "rank_changes_same_represented_inputs": int((actual.argmax(1) != represented.argmax(1)).sum()),
                    "rank_changes_vs_unquantized_fp64": int((actual.argmax(1) != ref.argmax(1)).sum()),
                    "admission_changes_at_hypothetical_gate": int((admission != (ref.max(1).values/variance >= gate)).sum()),
                    "hypothetical_correct_signal": int((positive_admission & (positive_prediction == truth)).sum()),
                    "hypothetical_wrong_signal": int((positive_admission & (positive_prediction != truth)).sum()),
                    "hypothetical_null_gate_passes": int(admission[:a.trials].sum()),
                    "calibrated_counts": None, "operationally_calibrated": False})
                row["timing_seconds"]["d2h_fp64_reference_and_verification"] = time.perf_counter()-verify_start
            except (RuntimeError, TypeError, NotImplementedError, AttributeError) as error:
                row["reason"] = f"requested operator failed without fallback: {error}"
            rows.append(row)
    record = {"schema": "triad-padded-tensor-probe-v1", "status": "screening_only",
              "scope": "sample-domain complete fixed-window bank; nonlinear input quantization explicitly applied before projection",
              "waveform_reference": "exported complex64 sampled waveform with independent FP64 projection; no independent waveform generator",
              "command_argv": sys.argv, "environment": inventory(),
              "device": str(torch.cuda.get_device_properties(device)) if device.type == "cuda" else "CPU",
              "seed": a.seed, "split": a.split, "seed_recipe": base._seed.__name__+" in triad_precision_probe.py; same noise reused across CN0/modes, do not pool sweeps",
              "shape": {"candidates": c, "samples": samples, "columns": columns, "trials": a.trials, "ranks": ranks.tolist()},
              "hypothetical_gate": {"threshold": gate, "family": family, "attempts_per_day": attempts,
                                    "daily_budget": 1e-4, "details": gate_meta,
                                    "validity": "uncalibrated for every quantized observation mode; only one fixed known-window attempt exercised"},
              "inputs_sha256": {str(directory/name): base.sha256(directory/name) for name in
                                ("manifest.json", manifest.get("basis_file", "basis.c64"), manifest.get("signal_file", "signal.c64"))},
              "source_sha256": {f.name: base.sha256(f) for f in (Path(__file__), Path(base.__file__), Path(gpu.__file__))},
              "results": rows, "complete_wall_seconds_including_fp64_validation": time.perf_counter()-start}
    a.output.write_text(json.dumps(record, indent=2, allow_nan=False)+"\n")
    print(json.dumps({"output": str(a.output), "rows": len(rows), "status": record["status"]}))


if __name__ == "__main__":
    main()

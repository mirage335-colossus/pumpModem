"""Explicit consumer-GPU tensor arithmetic for exhaustive triad correlations.

FP16/BF16 products accumulate into FP32; TF32 and FP8 are separate policies.
INT8 products accumulate into INT32 before conversion. No format is called
strict FP32 merely because its output is FP32. Adaptive observation scaling
changes its noise distribution and grants no operational admission calibration.

Triton paths require inspected dense tensor instructions. The portable CUDA
Torch FP16 path also supports Turing, but its library-selected instructions
remain unverified until an external execution profiler establishes them. CPU
pack/decode functions are representation oracles, never execution fallbacks.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
import platform
import re
import shutil
import subprocess
import time

import torch


FORMATS = ("fp16", "bf16", "tf32", "fp8-e4m3", "fp8-e5m2", "int8")
_MIN_CC = {"fp16": (7, 5), "bf16": (8, 0), "tf32": (8, 0),
           "fp8-e4m3": (8, 9), "fp8-e5m2": (8, 9), "int8": (7, 5)}
_TARGET_EXP = {"fp16": 0, "bf16": 0, "tf32": 0,
               "fp8-e4m3": 7, "fp8-e5m2": 13, "int8": 6}
_DTYPES = {"fp16": torch.float16, "bf16": torch.bfloat16, "tf32": torch.float32,
           "fp8-e4m3": torch.float8_e4m3fn, "fp8-e5m2": torch.float8_e5m2,
           "int8": torch.int8}


def capability(format, compute_capability, implementation="auto"):
    """Pure hardware/software dispatch policy; execution verification is separate."""
    if format not in FORMATS:
        raise ValueError(f"format must be one of {FORMATS}")
    if implementation not in ("auto", "triton", "torch"):
        raise ValueError("implementation must be auto, triton or torch")
    cc = tuple(compute_capability)
    if len(cc) != 2 or any(not isinstance(x, int) or isinstance(x, bool) for x in cc):
        raise ValueError("compute capability must be two integers")
    hardware = cc >= _MIN_CC[format]
    selected = implementation
    if selected == "auto":
        selected = "torch" if cc < (8, 0) and format == "fp16" else "triton"
    # Modern Triton does not promise Turing tensor-code generation. Do not let
    # an FMA lowering be silently reported as a tensor implementation.
    software = (cc >= (8, 0) if selected == "triton" else format in ("fp16", "bf16"))
    return {"format": format, "compute_capability": list(cc),
            "minimum_compute_capability": list(_MIN_CC[format]),
            "hardware_format_supported": hardware,
            "implementation": selected, "implementation_policy_supported": software,
            "supported": hardware and software, "executed_instructions_verified": False}


@dataclass(frozen=True)
class PackedTensorRows:
    data: torch.Tensor
    exponents: torch.Tensor
    diagnostics: torch.Tensor
    shape: tuple[int, int]
    padded_shape: tuple[int, int]
    format: str

    @property
    def nbytes(self):
        return sum(x.numel()*x.element_size() for x in
                   (self.data, self.exponents, self.diagnostics))

    @property
    def device(self):
        return self.data.device


def _validate_rows(rows, format, block_k):
    if format not in FORMATS:
        raise ValueError(f"format must be one of {FORMATS}")
    if not isinstance(rows, torch.Tensor) or rows.ndim != 2 or min(rows.shape) < 1:
        raise ValueError("packing requires positive [rows,K] tensor")
    if rows.dtype != torch.float32 or not rows.is_contiguous() or rows.requires_grad:
        raise ValueError("packing requires contiguous FP32 input without autograd")
    if block_k not in (32, 64, 128):
        raise ValueError("block_k must be 32, 64 or 128")
    if rows.shape[1] > 32768:
        raise ValueError("bounded tensor products require K<=32768; tile long sums explicitly")


def _ldexp_f32(x, exponents):
    """Declared three-rounding-stage FP32 normalization policy.

    Each power is normal and finite. This avoids spurious scale overflow, but
    near subnormal boundaries three RNE products may differ by one ulp from a
    single ideal ldexp. Product restoration instead uses one FP64 multiply.
    """
    first = exponents.clamp(-120, 120)
    remaining = exponents-first
    second = remaining.clamp(-120, 120)
    return torch.ldexp(torch.ldexp(torch.ldexp(x, first), second), remaining-second)


def _tf32_round(x):
    # RNE to ten fraction bits. Inputs are finite scaled values near unity;
    # exponent overflow is impossible here, including a rounding carry.
    bits = x.view(torch.int32)
    rounded = bits + 0xfff + ((bits >> 13) & 1)
    return (rounded & -8192).view(torch.float32)


def _fp8_preround_cpu(x, format):
    """F32-to-FP8 RNE lattice oracle, before an exact F16/FP8 conversion hop."""
    if format not in ("fp8-e4m3", "fp8-e5m2") or x.device.type != "cpu" or x.dtype != torch.float32:
        raise ValueError("FP8 lattice oracle requires CPU FP32 and an FP8 format")
    fraction, minimum_step = (3, -9) if format == "fp8-e4m3" else (2, -16)
    bits = x.abs().view(torch.int32)
    exponent = ((bits >> 23) & 255)-127
    step = (exponent-fraction).clamp_min(minimum_step)
    power = ((step+127) << 23).view(torch.float32)
    inverse = ((127-step) << 23).view(torch.float32)
    rounded = torch.round(x.abs()*inverse)*power
    return torch.where(torch.signbit(x), -rounded, rounded)


def _pack(rows, format, block_k):
    _validate_rows(rows, format, block_k)
    m, k = rows.shape
    kp = math.ceil(k/block_k)*block_k
    finite = torch.isfinite(rows)
    clean = torch.where(finite, rows, 0.)
    maximum = clean.abs().amax(1)
    _, frexp_exp = torch.frexp(maximum)
    exponent = frexp_exp.to(torch.int32)-1-_TARGET_EXP[format]
    exponent = torch.where(maximum == 0., 0, exponent)
    normalized = _ldexp_f32(clean, -exponent[:, None])
    if format == "tf32":
        data = _tf32_round(normalized)
        saturation = torch.zeros_like(maximum)
    elif format == "int8":
        rounded = torch.round(normalized)
        saturation = (rounded.abs() > 127.).sum(1).float()
        data = rounded.clamp(-127, 127).to(torch.int8)
    else:
        limit = torch.finfo(_DTYPES[format]).max
        saturation = (normalized.abs() > limit).sum(1).float()
        data = normalized.clamp(-limit, limit).to(_DTYPES[format])
    decoded = data.float()
    underflow = ((decoded == 0) & (clean != 0)).sum(1).float()
    diagnostics = torch.stack(((~finite).sum(1).float(), saturation, underflow, maximum), 1)
    if kp != k:
        # Float8 pad is unsupported in some Torch builds. Padding FP32 before
        # recasting keeps exact zero without extending the physical window.
        data = torch.nn.functional.pad(data.float(), (0, kp-k)).to(_DTYPES[format])
    return PackedTensorRows(data.contiguous(), exponent.contiguous(), diagnostics,
                            (m, k), (m, kp), format)


def pack_rows_cpu(rows, format="fp16", *, block_k=32):
    """Explicit CPU representation oracle using RNE conversions and exact scales."""
    if rows.device.type != "cpu":
        raise ValueError("CPU representation oracle requires CPU input")
    _validate_rows(rows, format, block_k)
    if not bool(torch.isfinite(rows).all()):
        raise ValueError("CPU representation oracle requires finite observations")
    return _pack(rows, format, block_k)


def decode_rows(packed, *, dtype=torch.float64):
    if dtype not in (torch.float32, torch.float64):
        raise ValueError("representation decode requires FP32 or FP64")
    values = packed.data[:, :packed.shape[1]].to(dtype)
    if dtype == torch.float64:
        return torch.ldexp(values, packed.exponents[:, None])
    return _ldexp_f32(values, packed.exponents[:, None])


def _native_ptx_evidence(ptx, format):
    suffix = {"fp16": ".f32.f16.f16.f32", "bf16": ".f32.bf16.bf16.f32",
              "tf32": ".f32.tf32.tf32.f32", "fp8-e4m3": ".f32.e4m3.e4m3.f32",
              "fp8-e5m2": ".f32.e5m2.e5m2.f32", "int8": ".s32.s8.s8.s32"}[format]
    statements = [x.strip() for x in ptx.split(";") if
                  ("mma.sync" in x or "wgmma.mma_async" in x) and suffix in x
                  and ".sp." not in x and "::sp" not in x]
    if not statements:
        raise RuntimeError(f"compiled {format} product lacks matching dense tensor instruction; no fallback accepted")
    return {"dense_tensor_instructions": statements, "native_ptx_verified": True,
            "accumulator": "int32" if format == "int8" else "fp32",
            "execution_result_validation_required": True}


@lru_cache(maxsize=1)
def _scaling_helpers():
    import triton
    import triton.language as tl

    @triton.jit
    def staged_f32(x, exponent):
        e1 = tl.minimum(tl.maximum(exponent, -120), 120)
        remainder = exponent-e1
        e2 = tl.minimum(tl.maximum(remainder, -120), 120)
        e3 = remainder-e2
        p1 = ((e1+127).to(tl.uint32) << 23).to(tl.float32, bitcast=True)
        p2 = ((e2+127).to(tl.uint32) << 23).to(tl.float32, bitcast=True)
        p3 = ((e3+127).to(tl.uint32) << 23).to(tl.float32, bitcast=True)
        first = tl.inline_asm_elementwise("mul.rn.f32 $0, $1, $2;", constraints="=f,f,f",
                                         args=[x, p1], dtype=tl.float32, is_pure=True, pack=1)
        second = tl.inline_asm_elementwise("mul.rn.f32 $0, $1, $2;", constraints="=f,f,f",
                                          args=[first, p2], dtype=tl.float32, is_pure=True, pack=1)
        return tl.inline_asm_elementwise("mul.rn.f32 $0, $1, $2;", constraints="=f,f,f",
                                        args=[second, p3], dtype=tl.float32, is_pure=True, pack=1)

    @triton.jit
    def restored_f64(x, exponent):
        # Combined row exponents are within[-324,254], so this power is an
        # exact normal FP64 value. Convert the FP32 accumulator exactly, apply
        # one binary scale, then round once to the declared FP32 output.
        power = ((exponent.to(tl.uint64)+1023) << 52).to(tl.float64, bitcast=True)
        return (x.to(tl.float64)*power).to(tl.float32)

    return staged_f32, restored_f64


@lru_cache(maxsize=1)
def _kernels():
    try:
        import triton
        import triton.language as tl
        from triton.language.extra import libdevice
    except ImportError as error:
        raise RuntimeError("selected tensor implementation requires Triton; no fallback executed") from error
    _, restored_f64 = _scaling_helpers()

    @triton.jit
    def product(A, B, EA, EB, C, M: tl.constexpr, N: tl.constexpr, K: tl.constexpr,
                BM: tl.constexpr, BN: tl.constexpr, BK: tl.constexpr,
                INTEGER: tl.constexpr, TF32: tl.constexpr, FLOAT8: tl.constexpr,
                PARTIAL: tl.constexpr):
        mi = tl.program_id(0)*BM+tl.arange(0, BM)
        ni = tl.program_id(1)*BN+tl.arange(0, BN)
        kk = tl.arange(0, BK)
        if INTEGER:
            acc = tl.full((BM, BN), 0, tl.int32)
        else:
            acc = tl.full((BM, BN), 0., tl.float32)
        for block in range(tl.cdiv(K, BK)):
            ki = block*BK+kk
            if INTEGER:
                a = tl.load(A+mi[:, None]*K+ki[None, :], (mi[:, None] < M) & (ki[None, :] < K), other=0)
                b = tl.load(B+ni[None, :]*K+ki[:, None], (ni[None, :] < N) & (ki[:, None] < K), other=0)
            else:
                # Triton3.4 cannot cast an int32 masked-load fill directly to
                # FP8. A floating zero preserves the intended exact padding.
                a = tl.load(A+mi[:, None]*K+ki[None, :], (mi[:, None] < M) & (ki[None, :] < K), other=0.)
                b = tl.load(B+ni[None, :]*K+ki[:, None], (ni[None, :] < N) & (ki[:, None] < K), other=0.)
            if PARTIAL:
                zero = tl.full((BM, BN), 0., tl.float32)
                if TF32:
                    partial = tl.dot(a, b, zero, input_precision="tf32")
                elif FLOAT8:
                    partial = tl.dot(a, b, zero, max_num_imprecise_acc=0)
                else:
                    partial = tl.dot(a, b, zero)
                acc = tl.inline_asm_elementwise("add.rn.f32 $0, $1, $2;", constraints="=f,f,f",
                                                args=[acc, partial], dtype=tl.float32, is_pure=True, pack=1)
            elif TF32:
                acc = tl.dot(a, b, acc, input_precision="tf32")
            elif FLOAT8:
                acc = tl.dot(a, b, acc, max_num_imprecise_acc=0)
            else:
                acc = tl.dot(a, b, acc)
        ea = tl.load(EA+mi, mi < M, other=0)
        eb = tl.load(EB+ni, ni < N, other=0)
        result = restored_f64(acc.to(tl.float32), ea[:, None]+eb[None, :])
        tl.store(C+mi[:, None]*N+ni[None, :], result, (mi[:, None] < M) & (ni[None, :] < N))

    return triton, product


@lru_cache(maxsize=1)
def _packing_kernel():
    try:
        import triton
        import triton.language as tl
        from triton.language.extra import libdevice
    except ImportError as error:
        raise RuntimeError("selected fused packing requires Triton; no fallback executed") from error
    staged_f32, _ = _scaling_helpers()

    @triton.jit
    def fp8_preround(x, FRACTION: tl.constexpr, MINIMUM_STEP: tl.constexpr):
        absolute = tl.abs(x)
        bits = absolute.to(tl.uint32, bitcast=True)
        exponent = ((bits >> 23) & 255).to(tl.int32)-127
        step = tl.maximum(exponent-FRACTION, MINIMUM_STEP)
        inverse = ((127-step).to(tl.uint32) << 23).to(tl.float32, bitcast=True)
        power = ((step+127).to(tl.uint32) << 23).to(tl.float32, bitcast=True)
        rounded = libdevice.nearbyint(absolute*inverse)*power
        # Float negation/select may be simplified to positive zero by a
        # compiler. Preserve the source sign in integer bits, including -0.
        magnitude = rounded.to(tl.uint32, bitcast=True) & 0x7fffffff
        sign = x.to(tl.uint32, bitcast=True) & 0x80000000
        return (magnitude | sign).to(tl.float32, bitcast=True)

    @triton.jit
    def pack(Source, Data, Exponent, Diagnostics,
             K: tl.constexpr, KP: tl.constexpr, P: tl.constexpr,
             FORMAT: tl.constexpr, TARGET: tl.constexpr):
        row = tl.program_id(0)
        col = tl.arange(0, P)
        x = tl.load(Source+row*K+col, col < K, other=0.)
        finite = tl.abs(x) < float("inf")
        clean = tl.where(finite, x, 0.)
        maximum = tl.max(tl.abs(clean), 0)
        bits = maximum.to(tl.uint32, bitcast=True) & 0x7fffffff
        field = ((bits >> 23) & 255).to(tl.int32)
        # Exact floor(log2) for normal and subnormal finite FP32 maxima.
        subnormal_exponent = 31-libdevice.clz(bits.to(tl.int32))-149
        floor_exponent = tl.where(field > 0, field-127, subnormal_exponent)
        exponent = tl.where(maximum == 0., 0, floor_exponent-TARGET)
        normalized = staged_f32(clean, -exponent)
        saturation = tl.full((P,), False, tl.int1)
        if FORMAT == 0:  # FP16
            saturation = tl.abs(normalized) > 65504.
            encoded = tl.minimum(tl.maximum(normalized, -65504.), 65504.).to(tl.float16)
        elif FORMAT == 1:  # BF16
            saturation = tl.abs(normalized) > 3.3895313892515355e38
            encoded = tl.minimum(tl.maximum(normalized, -3.3895313892515355e38), 3.3895313892515355e38).to(tl.bfloat16)
        elif FORMAT == 2:  # TF32 explicit RNE bit policy
            raw = normalized.to(tl.uint32, bitcast=True)
            raw = (raw+0xfff+((raw >> 13) & 1)) & 0xffffe000
            encoded = raw.to(tl.float32, bitcast=True)
        elif FORMAT == 3:  # FP8 E4M3, finite encoding
            saturation = tl.abs(normalized) > 448.
            clipped = tl.minimum(tl.maximum(normalized, -448.), 448.)
            encoded = fp8_preround(clipped, 3, -9).to(tl.float8e4nv)
        elif FORMAT == 4:  # FP8 E5M2
            saturation = tl.abs(normalized) > 57344.
            clipped = tl.minimum(tl.maximum(normalized, -57344.), 57344.)
            encoded = fp8_preround(clipped, 2, -16).to(tl.float8e5)
        else:  # INT8 nearest/even, then explicit symmetric saturation
            rounded = libdevice.nearbyint(normalized)
            saturation = tl.abs(rounded) > 127.
            encoded = tl.minimum(tl.maximum(rounded, -127.), 127.).to(tl.int8)
        tl.store(Data+row*KP+col, encoded, col < KP)
        tl.store(Exponent+row, exponent)
        bad = tl.sum(((~finite) & (col < K)).to(tl.int32), 0)
        clipped = tl.sum((saturation & (col < K)).to(tl.int32), 0)
        zeroed = tl.sum(((encoded.to(tl.float32) == 0.) & (clean != 0.) & (col < K)).to(tl.int32), 0)
        tl.store(Diagnostics+row*4, bad.to(tl.float32))
        tl.store(Diagnostics+row*4+1, clipped.to(tl.float32))
        tl.store(Diagnostics+row*4+2, zeroed.to(tl.float32))
        tl.store(Diagnostics+row*4+3, maximum)

    return triton, pack


class TensorBackend:
    """Immutable arithmetic policy, mutable compiled-artifact evidence only."""
    _FIELDS = frozenset(("format", "implementation", "engine", "block_m", "block_n",
                         "block_k", "num_warps", "num_stages", "group_size", "scaling",
                         "pack_implementation", "accumulation"))

    def __setattr__(self, name, value):
        if name in self._FIELDS and hasattr(self, name):
            raise AttributeError("tensor arithmetic policy is immutable; construct a new backend")
        super().__setattr__(name, value)

    def __init__(self, format="fp16", *, implementation="auto", engine=None,
                 block_m=64, block_n=64, block_k=32, num_warps=4, num_stages=2,
                 pack_implementation="torch", accumulation="native"):
        if engine is not None:
            if implementation != "auto" and implementation != engine:
                raise ValueError("conflicting engine and implementation")
            implementation = engine
        capability(format, (8, 9), implementation)
        if block_m not in (32, 64, 128) or block_n not in (32, 64, 128):
            raise ValueError("tensor tile M/N must be 32, 64 or 128")
        if block_k not in (32, 64, 128) or num_warps not in (4, 8) or num_stages not in (1, 2, 3, 4):
            raise ValueError("unsupported tensor tile K/warps/stages")
        if pack_implementation not in ("torch", "triton"):
            raise ValueError("pack_implementation must be torch or triton")
        if accumulation not in ("native", "partial-fp32"):
            raise ValueError("accumulation must be native or partial-fp32")
        if accumulation == "partial-fp32" and (block_k != 32 or format == "int8" or implementation == "torch"):
            raise ValueError("partial-fp32 requires floating Triton products and block_k=32")
        self.format, self.implementation, self.engine = format, implementation, implementation
        self.block_m, self.block_n, self.block_k = block_m, block_n, block_k
        self.num_warps, self.num_stages = num_warps, num_stages
        self.group_size, self.scaling = 1, "power-of-two-per-row"
        self.pack_implementation = pack_implementation
        self.accumulation = accumulation
        self._artifacts = {}
        self._pack_artifacts = {}
        self._resolved = {}

    def contract_metadata(self):
        return {"format": self.format, "storage": str(_DTYPES[self.format]),
                "multiplication": self.format, "accumulation": "int32" if self.format == "int8" else "fp32",
                "accumulation_algorithm": self.accumulation,
                "partial_sum_k": 32 if self.accumulation == "partial-fp32" else None,
                "output": "fp32", "scaling": self.scaling,
                "normalization_arithmetic": "three-stage-nonFTZ-FP32-RNE-power-multiplication",
                "normalization_single_ldexp_equivalent": False,
                "product_restoration_arithmetic": "exact-FP32-to-FP64, one-power-multiply, RNE-to-FP32",
                "pack_implementation": self.pack_implementation,
                "rounding": "nearest-even", "requested_implementation": self.implementation,
                "fused_fp8_conversion": "pre-round-FP32-to-exact-FP8-lattice-before-cast",
                "row_scale_target_binary_exponent": _TARGET_EXP[self.format],
                "tile_mnk": [self.block_m, self.block_n, self.block_k],
                "warps": self.num_warps, "stages": self.num_stages,
                "fp8_max_num_imprecise_acc": 0 if self.format.startswith("fp8") else None,
                "int8_overflow_bound": "K<=32768; abs(inputs)<=127 => sum<=528515072",
                "admission_calibrated": False}

    def runtime_metadata(self):
        return {**self.contract_metadata(), "resolved_implementations": dict(self._resolved),
                "native_ptx_verified": bool(self._artifacts),
                "fused_pack_compiled": bool(self._pack_artifacts),
                "torch_library_tensor_instructions_verified": False}

    describe = runtime_metadata

    def _dispatch(self, device):
        if device.type != "cuda":
            raise RuntimeError("tensor arithmetic requires CUDA; CPU oracles are explicit")
        cc = torch.cuda.get_device_capability(device)
        decision = capability(self.format, cc, self.implementation)
        if not decision["supported"]:
            raise RuntimeError(f"unsupported tensor arithmetic policy: {decision}")
        selected = decision["implementation"]
        self._resolved[str(device)] = selected
        return selected

    def estimated_packed_nbytes(self, rows, k):
        if any(not isinstance(x, int) or isinstance(x, bool) or x < 1 for x in (rows, k)):
            raise ValueError("rows and k must be positive integers")
        kp = math.ceil(k/self.block_k)*self.block_k
        return rows*kp*torch.empty((), dtype=_DTYPES[self.format]).element_size()+rows*(4+16)

    def estimated_pack_scratch_nbytes(self, rows, k):
        """Conservative Torch pack temporary bound, excluding returned pack."""
        self.estimated_packed_nbytes(rows, k)  # Validate the same shape contract.
        kp = math.ceil(k/self.block_k)*self.block_k
        return rows*kp*80+rows*256

    def estimated_matmul_scratch_nbytes(self, m, n, k):
        """Conservative bound excluding returned product and packed operands.

        Auto includes a64MiB reserve for possible Torch/cuBLAS workspace plus
        broad elementwise epilogue temporaries. This is an allocation estimate,
        not measured traffic or a guarantee about a different library version.
        """
        self.estimated_packed_nbytes(m, k)
        self.estimated_packed_nbytes(n, k)
        return 0 if self.implementation == "triton" else m*n*128+64*1024*1024

    def pack_rows(self, rows):
        self._dispatch(rows.device)
        if self.pack_implementation == "torch":
            return _pack(rows, self.format, self.block_k)
        _validate_rows(rows, self.format, self.block_k)
        if torch.cuda.get_device_capability(rows.device) < (8, 0):
            raise RuntimeError("fused Triton packing requires SM80+; select explicit Torch packing on Turing")
        triton, pack = _packing_kernel()
        m, k = rows.shape
        kp = math.ceil(k/self.block_k)*self.block_k
        data = torch.empty((m, kp), dtype=_DTYPES[self.format], device=rows.device)
        exponents = torch.empty((m,), dtype=torch.int32, device=rows.device)
        diagnostics = torch.empty((m, 4), dtype=torch.float32, device=rows.device)
        kernel = pack[(m,)](rows, data, exponents, diagnostics, k, kp, triton.next_power_of_2(kp),
                           FORMATS.index(self.format), _TARGET_EXP[self.format],
                           num_warps=8 if kp >= 2048 else 4, enable_fp_fusion=False)
        key = (str(rows.device), tuple(rows.shape))
        if key not in self._pack_artifacts:
            self._pack_artifacts[key] = {"kernel": kernel,
                                         "evidence": {"stage": "fused_pack", "format": self.format,
                                                      "paired_bitwise_validation_required": True}}
        return PackedTensorRows(data, exponents, diagnostics, (m, k), (m, kp), self.format)

    @staticmethod
    def packed_nbytes(packed):
        return packed.nbytes

    @staticmethod
    def decode_rows(packed, *, dtype=torch.float64):
        return decode_rows(packed, dtype=dtype)

    decode = decode_rows

    def matmul(self, a, b):
        if a.format != self.format or b.format != self.format or a.device != b.device:
            raise ValueError("packed formats/devices must match the backend")
        if a.shape[1] != b.shape[1] or a.padded_shape[1] != b.padded_shape[1]:
            raise ValueError("packed product K dimensions differ")
        implementation = self._dispatch(a.device)
        if implementation == "torch":
            if self.accumulation != "native":
                raise RuntimeError("partial-fp32 requires Triton; no library fallback executed")
            flag = "allow_fp16_reduced_precision_reduction" if self.format == "fp16" else "allow_bf16_reduced_precision_reduction"
            if getattr(torch.backends.cuda.matmul, flag):
                raise RuntimeError(f"FP32 accumulation requires caller to explicitly disable {flag}")
            if self.format == "fp16" and getattr(torch.backends.cuda.matmul, "allow_fp16_accumulation", False):
                raise RuntimeError("FP32 accumulation requires allow_fp16_accumulation=False")
            try:
                acc = torch.mm(a.data, b.data.T, out_dtype=torch.float32)
            except (TypeError, RuntimeError) as error:
                raise RuntimeError("CUDA Torch FP32-output low-precision mm unavailable; no fallback executed") from error
            exponent = a.exponents[:, None]+b.exponents[None, :]
            return torch.ldexp(acc.double(), exponent).float()
        triton, product = _kernels()
        result = torch.empty((a.shape[0], b.shape[0]), dtype=torch.float32, device=a.device)
        kernel = product[(triton.cdiv(a.shape[0], self.block_m), triton.cdiv(b.shape[0], self.block_n))](
            a.data, b.data, a.exponents, b.exponents, result,
            a.shape[0], b.shape[0], a.padded_shape[1], self.block_m, self.block_n, self.block_k,
            self.format == "int8", self.format == "tf32", self.format.startswith("fp8"),
            self.accumulation == "partial-fp32", num_warps=self.num_warps,
            num_stages=self.num_stages, enable_fp_fusion=False)
        key = (str(a.device), a.shape, b.shape)
        if key not in self._artifacts:
            evidence = _native_ptx_evidence(kernel.asm["ptx"], self.format)
            if self.accumulation == "partial-fp32":
                if "add.rn.f32" not in kernel.asm["ptx"]:
                    raise RuntimeError("compiled partial accumulation lacks explicit FP32 outer addition")
                evidence["explicit_partial_fp32_add_verified"] = True
            self._artifacts[key] = {"kernel": kernel, "evidence": evidence}
        return result

    def write_artifacts(self, directory):
        directory = Path(directory)
        directory.mkdir(parents=True, exist_ok=False)
        records = []
        all_artifacts = [("tensor", key, item) for key, item in self._artifacts.items()]
        all_artifacts += [("pack", key, item) for key, item in self._pack_artifacts.items()]
        for index, (kind, key, item) in enumerate(all_artifacts):
            stem = directory/f"{kind}-{index:03d}"
            files = {}
            for suffix in ("ptx", "cubin"):
                payload = item["kernel"].asm.get(suffix)
                if payload is None:
                    continue
                path = stem.with_suffix("."+suffix)
                path.write_bytes(payload.encode() if isinstance(payload, str) else payload)
                files[suffix] = {"name": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
            cuobjdump = shutil.which("cuobjdump")
            if cuobjdump and "cubin" in files:
                command = [cuobjdump, "--dump-sass", str(stem.with_suffix(".cubin"))]
                proc = subprocess.run(command, capture_output=True, text=True, timeout=30)
                path = stem.with_suffix(".sass")
                path.write_text(proc.stdout+proc.stderr)
                files["sass"] = {"name": path.name, "returncode": proc.returncode,
                                 "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
            records.append({"shape_key": str(key), **item["evidence"], "files": files})
        return records


NativeTensorBackend = TensorBackend


def _timed(function, repeats):
    times = []
    for _ in range(repeats):
        start, end = torch.cuda.Event(enable_timing=True), torch.cuda.Event(enable_timing=True)
        torch.cuda.synchronize()
        wall = time.perf_counter()
        start.record()
        result = function()
        end.record()
        end.synchronize()
        times.append({"cuda_ms": start.elapsed_time(end), "wall_seconds": time.perf_counter()-wall})
    return {"samples": times, "median_cuda_ms": sorted(x["cuda_ms"] for x in times)[len(times)//2],
            "median_wall_seconds": sorted(x["wall_seconds"] for x in times)[len(times)//2]}


def _small_product_metrics(original, represented, actual):
    """Report each selected small product without a dominant-score denominator.

    Exact FP32 rounding is meaningful for the selector case: each output has
    one nonzero represented multiplication and no cancellation/reduction error.
    This explicitly exposes hardware subnormal flushing even when the broad
    absolute-error bound used for ordinary GEMMs would accept the tiny error.
    """
    rounded_reference = represented.float().double()
    return {"original_fp64_products": original.tolist(),
            "represented_fp64_products": represented.tolist(),
            "rounded_represented_fp32_products": rounded_reference.tolist(),
            "actual_fp32_products": actual.tolist(),
            "representation_zeroed_nonzero_products": int(((represented == 0) & (original != 0)).sum()),
            "arithmetic_zeroed_nonzero_represented_products": int(((actual == 0) & (represented != 0)).sum()),
            "arithmetic_zeroed_nonzero_fp32_representable_products": int(((actual == 0) & (rounded_reference != 0)).sum()),
            "exact_fp32_rounding_pass": bool(torch.equal(actual, rounded_reference)),
            "relative_arithmetic_error_per_product": [
                [None if ref == 0. else abs(value-ref)/abs(ref)
                 for value, ref in zip(arow, rrow)]
                for arow, rrow in zip(actual.tolist(), represented.tolist())],
            "scope": "Each B row selects one small A component; the leading unit A component is never selected. No reduction or dominant-output denominator can hide loss."}


def _paired_diagnostic(backend, a, b, *, isolated_small_products=False):
    """Small actual products with distinct representation/arithmetic errors."""
    pa, pb = backend.pack_rows(a), backend.pack_rows(b)
    actual = backend.matmul(pa, pb).cpu().double()
    da, db = decode_rows(pa).cpu(), decode_rows(pb).cpu()
    original_a, original_b = a.cpu().double(), b.cpu().double()
    reference = da@db.T
    absolute = da.abs()@db.abs().T
    tolerance = absolute*(4*torch.finfo(torch.float32).eps*a.shape[1])+1e-30
    error = actual-reference
    strict = original_a@original_b.T
    variants = {"template_only": da@original_b.T,
                "observation_only": original_a@db.T, "both": reference}
    result = {"shape_mnk": [a.shape[0], b.shape[0], a.shape[1]],
            "native_finite": bool(torch.isfinite(actual).all()),
            "represented_product_max_abs_error": float(error.abs().max()),
            "represented_product_error_bound_pass": bool((error.abs() <= tolerance).all()),
            "representation_diagnostics": {
                key: {"max_abs_error_vs_original_fp64": float((value-strict).abs().max()),
                      "argmax_row_changes": int((value.argmax(0) != strict.argmax(0)).sum())}
                for key, value in variants.items()},
            "diagnostics_sum_a": pa.diagnostics[:, :3].sum(0).cpu().tolist(),
            "diagnostics_sum_b": pb.diagnostics[:, :3].sum(0).cpu().tolist(),
            "scope": "Synthetic paired arithmetic; not physical reception or admission"}
    if isolated_small_products:
        result["isolated_small_products"] = _small_product_metrics(strict, reference, actual)
    return result


def scaling_diagnostic(format, artifact_directory):
    """Causal GPU discriminator for raw MMA versus restoration/subnormal loss.

    Prototype scaling alternatives are diagnostics only. They do not change
    the selected backend's arithmetic or establish a performance preference.
    """
    import triton
    import triton.language as tl
    from triton.language.extra import libdevice

    @triton.jit
    def scales(X, E, Copy, Ldexp, Staged, Double, N: tl.constexpr, P: tl.constexpr):
        index = tl.arange(0, P)
        x = tl.load(X+index, index < N, other=0.)
        exponent = tl.load(E+index, index < N, other=0)
        library = libdevice.ldexp(x, exponent)
        e1 = tl.minimum(tl.maximum(exponent, -120), 120)
        remainder = exponent-e1
        e2 = tl.minimum(tl.maximum(remainder, -120), 120)
        e3 = remainder-e2
        p1 = ((e1+127).to(tl.uint32) << 23).to(tl.float32, bitcast=True)
        p2 = ((e2+127).to(tl.uint32) << 23).to(tl.float32, bitcast=True)
        p3 = ((e3+127).to(tl.uint32) << 23).to(tl.float32, bitcast=True)
        first = tl.inline_asm_elementwise("mul.rn.f32 $0, $1, $2;", constraints="=f,f,f",
                                         args=[x, p1], dtype=tl.float32, is_pure=True, pack=1)
        second = tl.inline_asm_elementwise("mul.rn.f32 $0, $1, $2;", constraints="=f,f,f",
                                          args=[first, p2], dtype=tl.float32, is_pure=True, pack=1)
        staged = tl.inline_asm_elementwise("mul.rn.f32 $0, $1, $2;", constraints="=f,f,f",
                                          args=[second, p3], dtype=tl.float32, is_pure=True, pack=1)
        power = ((exponent.to(tl.uint64)+1023) << 52).to(tl.float64, bitcast=True)
        double = (x.to(tl.float64)*power).to(tl.float32)
        tl.store(Copy+index, x, index < N)
        tl.store(Ldexp+index, library, index < N)
        tl.store(Staged+index, staged, index < N)
        tl.store(Double+index, double, index < N)

    @triton.jit
    def raw_dot(A, B, C, TF32: tl.constexpr):
        m = tl.arange(0, 32)
        n = tl.arange(0, 32)
        k = tl.arange(0, 32)
        a = tl.load(A+m[:, None]*32+k[None, :], m[:, None] < 1, other=0.)
        b = tl.load(B+n[None, :]*32+k[:, None], n[None, :] < 1, other=0.)
        if TF32:
            result = tl.dot(a, b, input_precision="tf32")
        else:
            result = tl.dot(a, b)
        tl.store(C+m[:, None]*32+n[None, :], result, (m[:, None] < 1) & (n[None, :] < 1))

    if format not in ("fp16", "bf16", "tf32"):
        raise ValueError("scaling diagnostic raw MMA supports fp16, bf16 and tf32")
    nonpower = torch.tensor([0x3c000005], dtype=torch.int32).view(torch.float32).item()
    values = [-torch.finfo(torch.float32).max, -1., -nonpower, -2.**-120, -2.**-126,
              -2.**-127, -2.**-140, -2.**-149, -0., 0., 2.**-149,
              2.**-140, 2.**-127, 2.**-126, 2.**-120, nonpower, 1., torch.finfo(torch.float32).max]
    exponents = [-300, -149, -127, -122, -120, -24, -1, 0, 1, 24, 120, 127, 149, 300]
    cpu_x = torch.tensor([x for x in values for e in exponents], dtype=torch.float32)
    cpu_e = torch.tensor([e for x in values for e in exponents], dtype=torch.int32)
    x, e = cpu_x.cuda(), cpu_e.cuda()
    outputs = [torch.empty_like(x) for _ in range(4)]
    kernel = scales[(1,)](x, e, *outputs, x.numel(), triton.next_power_of_2(x.numel()),
                          num_warps=4, enable_fp_fusion=False)
    reference = torch.ldexp(cpu_x.double(), cpu_e).float()
    converted = [output.cpu() for output in outputs]
    names = ["copy", "libdevice_ldexp", "staged_nonftz_mul", "fp64_power_mul"]
    rows = []
    for index, (value, exponent) in enumerate(zip(cpu_x.tolist(), cpu_e.tolist())):
        rows.append({"input": float(value).hex(), "exponent": exponent,
                     "reference_bits_hex": f"{reference.view(torch.int32)[index].item() & 0xffffffff:08x}",
                     **{name+"_bits_hex": f"{out.view(torch.int32)[index].item() & 0xffffffff:08x}"
                        for name, out in zip(names, converted)}})
    backend = TensorBackend(format, implementation="triton", pack_implementation="torch")
    a = torch.zeros((1, 32), dtype=torch.float32, device="cuda")
    b = torch.zeros_like(a)
    a[0, 0], a[0, 1], b[0, 1] = 1., 2.**-127, 1.
    pa, pb = backend.pack_rows(a), backend.pack_rows(b)
    raw = torch.empty((1, 1), device="cuda", dtype=torch.float32)
    raw_kernel = raw_dot[(1,)](pa.data, pb.data, raw, format == "tf32", num_warps=4)
    actual = backend.matmul(pa, pb)
    represented = decode_rows(pa).cpu()@decode_rows(pb).cpu().T
    raw_evidence = _native_ptx_evidence(raw_kernel.asm["ptx"], format)
    directory = Path(artifact_directory)
    if directory.exists():
        raise FileExistsError(directory)
    # Reuse the normal artifact writer for the raw product and scaling test,
    # while preserving all ordinary backend artifacts under distinct keys.
    backend._artifacts[("diagnostic-raw",)] = {"kernel": raw_kernel, "evidence": raw_evidence}
    backend._pack_artifacts[("diagnostic-scales",)] = {"kernel": kernel,
        "evidence": {"stage": "scaling_discriminator", "not_backend_optimization": True}}
    return {"schema": "triad-tensor-scaling-discriminator-v1", "format": format,
            "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "gpu": torch.cuda.get_device_name(), "torch": torch.__version__,
            "mismatches_vs_cpu_fp64_rounded_fp32": {
                name: int((out.view(torch.int32) != reference.view(torch.int32)).sum())
                for name, out in zip(names[1:], converted[1:])},
            "raw_copy_bit_mismatches": int((converted[0].view(torch.int32) != cpu_x.view(torch.int32)).sum()),
            "rows": rows,
            "raw_mma_selector": {"represented_fp64": represented.tolist(),
                                 "raw_fp32": raw.cpu().double().tolist(),
                                 "backend_restored_fp32": actual.cpu().double().tolist(),
                                 "a_exponents": pa.exponents.cpu().tolist(),
                                 "b_exponents": pb.exponents.cpu().tolist(),
                                 "raw_ptx": raw_evidence},
            "artifacts": backend.write_artifacts(directory),
            "scope": "Diagnostic alternatives only; no backend default or precision policy changed"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--format", choices=FORMATS, default="fp16")
    parser.add_argument("--implementation", "--engine", choices=("auto", "triton", "torch"), default="auto")
    parser.add_argument("--pack-implementation", choices=("torch", "triton"), default="torch")
    parser.add_argument("--accumulation", choices=("native", "partial-fp32"), default="native")
    parser.add_argument("--diagnose-scaling", action="store_true")
    parser.add_argument("--shape", default="128,128,128")
    parser.add_argument("--repeats", type=int, default=7)
    parser.add_argument("--seed", type=int, default=71051)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()
    shape = tuple(int(x) for x in args.shape.split(","))
    if len(shape) != 3 or min(shape) < 1 or args.repeats < 1:
        parser.error("shape must be positive M,N,K and repeats positive")
    if args.output.exists() or args.artifacts.exists():
        parser.error("probe output and artifacts must be new paths")
    torch.set_float32_matmul_precision("highest")
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cuda.matmul.allow_fp16_reduced_precision_reduction = False
    torch.backends.cuda.matmul.allow_bf16_reduced_precision_reduction = False
    if hasattr(torch.backends.cuda.matmul, "allow_fp16_accumulation"):
        torch.backends.cuda.matmul.allow_fp16_accumulation = False
    if args.diagnose_scaling:
        result = scaling_diagnostic(args.format, args.artifacts)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2)+"\n")
        return
    backend = TensorBackend(args.format, implementation=args.implementation,
                            pack_implementation=args.pack_implementation,
                            accumulation=args.accumulation)
    torch.manual_seed(args.seed)
    m, n, k = shape
    a = torch.randn((m, k), device="cuda", dtype=torch.float32)
    b = torch.randn((n, k), device="cuda", dtype=torch.float32)
    wall = time.perf_counter()
    pa, pb = backend.pack_rows(a), backend.pack_rows(b)
    actual = backend.matmul(pa, pb)
    torch.cuda.synchronize()
    cold = time.perf_counter()-wall
    # A bounded independent represented-input oracle avoids materializing a
    # huge FP64 GEMM solely to validate a large timing shape.
    am, bn = min(m, 32), min(n, 32)
    da, db = decode_rows(pa)[:am].cpu(), decode_rows(pb)[:bn].cpu()
    reference = da@db.T
    error = actual[:am, :bn].cpu().double()-reference
    arithmetic_tolerance = (da.abs()@db.abs().T)*(4*torch.finfo(torch.float32).eps*k)
    arithmetic_pass = bool((error.abs() <= arithmetic_tolerance+1e-30).all())
    for _ in range(3):
        backend.matmul(pa, pb)
        torch.mm(a, b.T)
    torch.cuda.synchronize()
    timings = {"pack_a": _timed(lambda: backend.pack_rows(a), args.repeats),
               "pack_b": _timed(lambda: backend.pack_rows(b), args.repeats),
               "cached_product": _timed(lambda: backend.matmul(pa, pb), args.repeats),
               "cached_a_pack_b_product": _timed(lambda: backend.matmul(pa, backend.pack_rows(b)), args.repeats),
               "both_pack_product": _timed(lambda: backend.matmul(backend.pack_rows(a), backend.pack_rows(b)), args.repeats),
               "strict_fp32_product": _timed(lambda: torch.mm(a, b.T), args.repeats)}
    extreme_a = torch.full((2, 128), 1e37, device="cuda")
    extreme_a[1] *= -1.
    extreme_b = torch.full((2, 128), 1e-36, device="cuda")
    extreme_b[1, ::2] *= -1.
    cancellation_a = torch.ones((2, 4096), device="cuda")
    cancellation_a[1, ::2] += 2.**-10
    cancellation_b = torch.ones((2, 4096), device="cuda")
    cancellation_b[0, ::2] *= -1.
    cancellation_b[1, 1::2] *= -1.
    cases = {"opposite_extreme_row_scales": _paired_diagnostic(backend, extreme_a, extreme_b),
             "cancellation": _paired_diagnostic(backend, cancellation_a, cancellation_b),
             "long_sum": _paired_diagnostic(backend, torch.randn((2, 32768), device="cuda"),
                                             torch.randn((2, 32768), device="cuda"))}
    small_exponents = [-12, -20, -24, -30, -120, -127, -140, -149]
    selector_a = torch.tensor([[1.]+[2.**x for x in small_exponents]], device="cuda", dtype=torch.float32)
    selector_b = torch.zeros((len(small_exponents), len(small_exponents)+1), device="cuda")
    selector_b[:, 1:] = torch.eye(len(small_exponents), device="cuda")
    cases["within_row_small_selected_products"] = _paired_diagnostic(
        backend, selector_a, selector_b, isolated_small_products=True)
    cases["within_row_small_selected_products"]["selected_original_binary_exponents"] = small_exponents
    result = {"schema": "triad-consumer-tensor-probe-v1", "shape_mnk": shape, "seed": args.seed,
              "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "environment": {"python": platform.python_version(), "torch": torch.__version__,
                              "cuda": torch.version.cuda, "gpu": torch.cuda.get_device_name(),
                              "compute_capability": torch.cuda.get_device_capability()},
              "contract": backend.contract_metadata(), "execution": backend.runtime_metadata(),
              "cold_seconds": cold, "timings": timings,
              "represented_fp64_oracle": {"rows": [am, bn], "max_abs_error": float(error.abs().max()),
                                           "error_bound_pass": arithmetic_pass},
              "diagnostics_columns": ["nonfinite", "saturated", "rounded_to_zero", "row_max_abs"],
              "a_diagnostics": pa.diagnostics.cpu().tolist(), "b_diagnostics": pb.diagnostics.cpu().tolist(),
              "paired_numerical_cases": cases,
              "packed_bytes": [pa.nbytes, pb.nbytes], "artifacts": backend.write_artifacts(args.artifacts),
              "scope": "Synthetic arithmetic probe; not waveform sensitivity or calibrated admission"}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+"\n")
    if not arithmetic_pass or not all(x["native_finite"] and x["represented_product_error_bound_pass"]
                                      for x in cases.values()):
        raise RuntimeError("represented-input FP64 arithmetic oracle failed")


if __name__ == "__main__":
    main()

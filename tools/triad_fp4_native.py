"""Optional dense native FP4 correlation products; no receiver policy changes.

Both matrices are stored as rows of a common K dimension.  NVFP4 uses packed
E2M1 values and E4M3 scales per 16 values; MXFP4 uses E8M0 scales per 32.
An exact power-of-two FP32 row scale extends range and is restored in the
GEMM epilogue.  Scales round upward so finite group maxima do not saturate.
FP4 values round to nearest/even.  Adaptive observation scaling is nonlinear:
this module grants no Gaussian-null or operational admission calibration.

The named CPU routines are explicit representation oracles, never native
fallbacks. CUDA matmul requires a compiled dense block-scaled E2M1 instruction
with FP32 accumulator/output; unsupported dispatch fails rather than silently
executing a BF16/FP16 expansion.  Executed correctness and SASS remain separate
probe evidence, not consequences of a successful dtype/API call.
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
import traceback

import torch


FORMATS = ("nvfp4", "mxfp4")
_LEVELS = (0., .5, 1., 1.5, 2., 3., 4., 6.)


@dataclass
class PackedFp4Rows:
    data: torch.Tensor
    scales: torch.Tensor
    row_scales: torch.Tensor
    diagnostics: torch.Tensor
    shape: tuple[int, int]
    padded_shape: tuple[int, int]
    format: str

    @property
    def nbytes(self):
        return sum(x.numel()*x.element_size() for x in
                   (self.data, self.scales, self.row_scales, self.diagnostics))

    @property
    def device(self):
        return self.data.device


def _arguments(rows, format, block_k):
    if format not in FORMATS:
        raise ValueError(f"format must be one of {FORMATS}")
    if rows.ndim != 2 or min(rows.shape) < 1 or rows.dtype != torch.float32:
        raise ValueError("FP4 packing requires positive [rows,K] FP32 real input")
    if not rows.is_contiguous() or rows.requires_grad:
        raise ValueError("FP4 packing requires contiguous input without autograd")
    if block_k not in (128, 256, 512):
        raise ValueError("block_k must be 128, 256 or 512")
    if rows.shape[1] > 32768:
        raise ValueError("bounded fused pack supports K<=32768; tile longer sums explicitly")
    return 16 if format == "nvfp4" else 32


def _codes_cpu(x):
    a = x.abs()
    # Midpoints alternate the even representation index on ties.
    code = torch.where(a <= .25, 0, torch.where(a < .75, 1,
           torch.where(a <= 1.25, 2, torch.where(a < 1.75, 3,
           torch.where(a <= 2.5, 4, torch.where(a < 3.5, 5,
           torch.where(a <= 5., 6, 7))))))).to(torch.uint8)
    return code | (torch.signbit(x).to(torch.uint8) << 3)


def pack_rows_cpu(rows, format="nvfp4", *, block_k=128):
    """Independent CPU quantizer with explicit padding and represented scales."""
    group = _arguments(rows, format, block_k)
    if rows.device.type != "cpu":
        raise ValueError("CPU representation oracle requires CPU tensors")
    if not bool(torch.isfinite(rows).all()):
        raise ValueError("FP4 CPU oracle requires finite values")
    m, k = rows.shape
    kp = math.ceil(k/block_k)*block_k
    source = torch.zeros((m, kp), dtype=torch.float32)
    source[:, :k] = rows
    maximum = source.abs().amax(dim=1)
    # Exact exponent extraction avoids log2 rounding at powers of two.
    safe = maximum.clamp_min(2.**-120)
    exponent = ((safe.view(torch.int32) >> 23) & 255)-127-7
    global_scale = torch.exp2(exponent.clamp(-120, 120).float())
    global_scale[maximum == 0] = 1.
    normalized = (source/global_scale[:, None]).reshape(m, kp//group, group)
    local_max = normalized.abs().amax(dim=2)
    desired = local_max/6.
    if format == "nvfp4":
        desired = desired.clamp_min(2.**-9)
        scale = desired.to(torch.float8_e4m3fn)
        bits = scale.view(torch.uint8)
        bits = bits + (scale.float() < desired).to(torch.uint8)
        bits[local_max == 0] = 56
        scale = bits.view(torch.float8_e4m3fn)
        decoded_scale = scale.float()
    else:
        desired = desired.clamp_min(2.**-126)
        bits = desired.view(torch.int32)
        exp = ((bits >> 23) & 255)-127 + ((bits & 0x7fffff) != 0).int()
        scale = (exp+127).to(torch.uint8)
        scale[local_max == 0] = 127
        decoded_scale = torch.exp2(scale.int().float()-127.)
    scaled = normalized/decoded_scale[:, :, None]
    code = _codes_cpu(scaled).reshape(m, kp)
    data = (code[:, 0::2] | (code[:, 1::2] << 4)).contiguous()
    zeroed = ((code & 7) == 0) & (source != 0)
    diag = torch.stack((torch.zeros_like(maximum),
                        (scaled.abs() > 6.).sum(dim=(1, 2)).float(),
                        zeroed.sum(dim=1).float(), maximum), dim=1)
    return PackedFp4Rows(data, scale.contiguous(), global_scale, diag,
                         (m, k), (m, kp), format)


def decode_rows(packed, *, dtype=torch.float64):
    """Decode the represented operands on their current device, without GEMM."""
    group = 16 if packed.format == "nvfp4" else 32
    code = torch.stack((packed.data & 15, packed.data >> 4), dim=-1).reshape(packed.padded_shape)
    if dtype not in (torch.float32, torch.float64):
        raise ValueError("representation decode requires explicit FP32 or FP64 output")
    levels = torch.tensor(_LEVELS, dtype=dtype, device=packed.device)
    values = levels[(code & 7).long()]*torch.where((code & 8) != 0, -1., 1.)
    scale = (packed.scales.float() if packed.format == "nvfp4" else
             torch.exp2(packed.scales.int().float()-127.))
    result = values*scale.to(dtype).repeat_interleave(group, dim=1)*packed.row_scales.to(dtype)[:, None]
    return result[:, :packed.shape[1]]


def subset_rows(packed, count):
    """View a leading row subset before decoding large benchmark operands."""
    if not 1 <= count <= packed.shape[0]:
        raise ValueError("packed row subset outside original shape")
    return PackedFp4Rows(packed.data[:count], packed.scales[:count], packed.row_scales[:count],
                         packed.diagnostics[:count], (count, packed.shape[1]),
                         (count, packed.padded_shape[1]), packed.format)


def _restore_rows_cpu(accumulator, row_a, row_b):
    """CPU oracle for exact power-of-two epilogue, including extreme exponents."""
    ea = ((row_a.view(torch.int32) >> 23) & 255)-127
    eb = ((row_b.view(torch.int32) >> 23) & 255)-127
    # Torch's FP32 CPU ldexp currently materializes 2**exponent first and can
    # overflow/underflow even with a representable final value.  FP64 covers
    # the full supported combined exponent range before the output rounding.
    result = torch.ldexp(accumulator.double(), ea[:, None]+eb[None, :]).float()
    return torch.where(torch.isfinite(row_a[:, None]) & torch.isfinite(row_b[None, :]), result, float("nan"))


def _native_ptx_evidence(ptx):
    """Conservative instruction check: emulated scaled-dot cannot pass this."""
    statements = [s.strip() for s in ptx.split(";")
                  if ("mma.sync" in s or "tcgen05.mma" in s) and "e2m1" in s]
    dense = [s for s in statements if "block_scale" in s and ".f32" in s and
             ".sp." not in s and "::sp" not in s]
    if not dense:
        raise RuntimeError("compiled product lacks dense block-scaled E2M1 FP32 MMA; no native fallback accepted")
    return {"dense_fp4_fp32_instructions": dense,
            "sparse_instruction": any(".sp." in s or "::sp" in s for s in statements),
            "native_ptx_verified": True,
            "executed_result_validation_required": True}


@lru_cache(maxsize=1)
def _kernels():
    try:
        import triton
        import triton.language as tl
        from triton.language.extra import libdevice
    except ImportError as error:
        raise RuntimeError("native FP4 requires Triton; no fallback executed") from error
    version = tuple(int(v) for v in re.findall(r"\d+", triton.__version__)[:2])
    if version < (3, 8):
        raise RuntimeError(f"Triton{triton.__version__} predates the tested FP4 scale16 API; install>=3.8 (prefer>=3.9)")

    @triton.jit
    def pack(source, data, scales, globals_, diagnostics,
             K: tl.constexpr, KP: tl.constexpr, GROUP: tl.constexpr,
             NV: tl.constexpr, P: tl.constexpr):
        row = tl.program_id(0)
        col = tl.arange(0, P)
        x = tl.load(source+row*K+col, col < K, other=0.)
        finite = (tl.abs(x) < float("inf"))
        bad = tl.sum((~finite).to(tl.int32), 0)
        clean = tl.where(finite, x, 0.)
        maximum = tl.max(tl.abs(clean), 0)
        safe = tl.maximum(maximum, 7.52316384526264e-37)  # 2^-120
        exp = ((safe.to(tl.uint32, bitcast=True) >> 23) & 255).to(tl.int32)-127-7
        global_scale = tl.exp2(tl.minimum(tl.maximum(exp, -120), 120).to(tl.float32))
        global_scale = tl.where(maximum == 0., 1., global_scale)
        norm = tl.div_rn(clean, global_scale).reshape((P//GROUP, GROUP))
        local_max = tl.max(tl.abs(norm), 1)
        desired = tl.div_rn(local_max, 6.)
        if NV:
            desired = tl.maximum(desired, .001953125)
            rounded = desired.to(tl.float8e4nv)
            code_scale = rounded.to(tl.uint8, bitcast=True)
            code_scale += (rounded.to(tl.float32) < desired).to(tl.uint8)
            code_scale = tl.where(local_max == 0., 56, code_scale).to(tl.uint8)
            native_scale = code_scale.to(tl.float8e4nv, bitcast=True)
            scale_f32 = native_scale.to(tl.float32)
        else:
            desired = tl.maximum(desired, 1.1754943508222875e-38)
            bits = desired.to(tl.uint32, bitcast=True)
            exponent = ((bits >> 23) & 255).to(tl.int32)-127
            exponent += ((bits & 0x7fffff) != 0).to(tl.int32)
            code_scale = tl.where(local_max == 0., 127, exponent+127).to(tl.uint8)
            native_scale = code_scale
            scale_f32 = tl.exp2(code_scale.to(tl.float32)-127.)
        y = tl.div_rn(norm, scale_f32[:, None]).reshape((P,))
        a = tl.abs(y)
        code = tl.where(a <= .25, 0, tl.where(a < .75, 1,
               tl.where(a <= 1.25, 2, tl.where(a < 1.75, 3,
               tl.where(a <= 2.5, 4, tl.where(a < 3.5, 5,
               tl.where(a <= 5., 6, 7))))))).to(tl.uint8)
        code |= ((y.to(tl.uint32, bitcast=True) >> 28) & 8).to(tl.uint8)
        pair = code.reshape((P//2, 2))
        low, high = tl.split(pair)
        pc = tl.arange(0, P//2)
        tl.store(data+row*(KP//2)+pc, low | (high << 4), pc < KP//2)
        sc = tl.arange(0, P//GROUP)
        tl.store(scales+row*(KP//GROUP)+sc, native_scale, sc < KP//GROUP)
        # Invalid data is visible in the product, never silently replaced.
        tl.store(globals_+row, tl.where(bad > 0, float("nan"), global_scale))
        tl.store(diagnostics+row*4, bad.to(tl.float32))
        tl.store(diagnostics+row*4+1, tl.sum(((a > 6.) & (col < K)).to(tl.int32), 0).to(tl.float32))
        tl.store(diagnostics+row*4+2, tl.sum((((code & 7) == 0) & (clean != 0.) & (col < K)).to(tl.int32), 0).to(tl.float32))
        tl.store(diagnostics+row*4+3, maximum)

    @triton.jit
    def product(A, AS, AG, B, BS, BG, C,
                M: tl.constexpr, N: tl.constexpr, KP: tl.constexpr,
                GROUP: tl.constexpr, BM: tl.constexpr, BN: tl.constexpr, BK: tl.constexpr):
        mi = tl.program_id(0)*BM+tl.arange(0, BM)
        ni = tl.program_id(1)*BN+tl.arange(0, BN)
        pk = tl.arange(0, BK//2)
        sk = tl.arange(0, BK//GROUP)
        acc = tl.zeros((BM, BN), dtype=tl.float32)
        for kk in range(KP//BK):
            a = tl.load(A+mi[:, None]*(KP//2)+(kk*(BK//2)+pk[None, :]), mi[:, None] < M, other=0)
            b = tl.load(B+ni[:, None]*(KP//2)+(kk*(BK//2)+pk[None, :]), ni[:, None] < N, other=0)
            # Float padding is intentional: Triton cannot cast an integer
            # masked-load fallback directly to the E4M3 scale tensor type.
            sa = tl.load(AS+mi[:, None]*(KP//GROUP)+(kk*(BK//GROUP)+sk[None, :]), mi[:, None] < M, other=0.)
            sb = tl.load(BS+ni[:, None]*(KP//GROUP)+(kk*(BK//GROUP)+sk[None, :]), ni[:, None] < N, other=0.)
            acc = tl.dot_scaled(a, sa, "e2m1", b.T, sb, "e2m1", acc,
                                fast_math=False, lhs_k_pack=True, rhs_k_pack=True,
                                out_dtype=tl.float32)
        ag = tl.load(AG+mi, mi < M, other=0.)
        bg = tl.load(BG+ni, ni < N, other=0.)
        # Scaling one row at a time can overflow despite a finite final result
        # when the other row has the opposite exponent.  ldexp handles the sum
        # directly, including subnormal and genuinely overflowing outputs.
        ea = ((ag.to(tl.uint32, bitcast=True) >> 23) & 255).to(tl.int32)-127
        eb = ((bg.to(tl.uint32, bitcast=True) >> 23) & 255).to(tl.int32)-127
        result = libdevice.ldexp(acc, ea[:, None]+eb[None, :])
        result = tl.where((tl.abs(ag[:, None]) < float("inf")) &
                          (tl.abs(bg[None, :]) < float("inf")), result, float("nan"))
        tl.store(C+mi[:, None]*N+ni[None, :], result, (mi[:, None] < M) & (ni[None, :] < N))

    return triton, pack, product


class NativeFp4Backend:
    """Explicit optional CUDA backend; no host synchronization inside tiles."""
    def __init__(self, format="nvfp4", *, block_m=128, block_n=128, block_k=128,
                 num_warps=4, num_stages=2):
        if format not in FORMATS or block_m not in (128, 256) or block_n not in (64, 128, 256):
            raise ValueError("native FP4 requires supported format and BM128/256, BN64/128/256")
        if block_k not in (128, 256, 512) or num_warps not in (4, 8) or num_stages not in (1, 2, 3, 4):
            raise ValueError("unsupported bounded native FP4 tile configuration")
        self.format, self.block_m, self.block_n, self.block_k = format, block_m, block_n, block_k
        self.num_warps, self.num_stages = num_warps, num_stages
        self.last_kernel = None
        self.instruction_evidence = None
        self._verified_kernels = {}
        self.pack_calls = self.matmul_calls = 0

    @property
    def group_size(self):
        return 16 if self.format == "nvfp4" else 32

    def pack_rows(self, rows):
        group = _arguments(rows, self.format, self.block_k)
        if rows.device.type != "cuda":
            raise RuntimeError("native FP4 packing requires CUDA; use pack_rows_cpu only as explicit diagnostic")
        if torch.cuda.get_device_capability(rows.device)[0] not in (10, 12):
            raise RuntimeError("native FP4 study supports SM100/SM120 CUDA only; no fallback executed")
        triton, pack, _ = _kernels()
        m, k = rows.shape
        kp = math.ceil(k/self.block_k)*self.block_k
        scale_dtype = torch.float8_e4m3fn if self.format == "nvfp4" else torch.uint8
        result = PackedFp4Rows(torch.empty((m, kp//2), dtype=torch.uint8, device=rows.device),
                              torch.empty((m, kp//group), dtype=scale_dtype, device=rows.device),
                              torch.empty(m, dtype=torch.float32, device=rows.device),
                              torch.empty((m, 4), dtype=torch.float32, device=rows.device),
                              (m, k), (m, kp), self.format)
        pack[(m,)](rows, result.data, result.scales, result.row_scales, result.diagnostics,
                   K=k, KP=kp, GROUP=group, NV=self.format == "nvfp4", P=triton.next_power_of_2(kp),
                   num_warps=4, enable_fp_fusion=False)
        self.pack_calls += 1
        return result

    def matmul(self, a, b, *, out=None):
        if a.format != self.format or b.format != self.format or a.shape[1] != b.shape[1]:
            raise ValueError("FP4 product requires matching format and original K")
        if a.device.type != "cuda" or b.device != a.device or a.padded_shape[1] != b.padded_shape[1]:
            raise RuntimeError("native product requires same-device CUDA packed rows and matching padded K")
        m, n, kp = a.shape[0], b.shape[0], a.padded_shape[1]
        if kp % self.block_k:
            raise ValueError("packed K is incompatible with this backend's block_k")
        if out is None:
            out = torch.empty((m, n), dtype=torch.float32, device=a.device)
        if out.shape != (m, n) or out.dtype != torch.float32 or out.device != a.device or not out.is_contiguous():
            raise ValueError("native FP4 output requires contiguous FP32 [M,N] on operand device")
        triton, _, kernel = _kernels()
        compiled = kernel[(triton.cdiv(m, self.block_m), triton.cdiv(n, self.block_n))](
            a.data, a.scales, a.row_scales, b.data, b.scales, b.row_scales, out,
            M=m, N=n, KP=kp, GROUP=16 if self.format == "nvfp4" else 32,
            BM=self.block_m, BN=self.block_n, BK=self.block_k,
            num_warps=self.num_warps, num_stages=self.num_stages,
            enable_fp_fusion=False)
        kernel_id = compiled.hash
        if kernel_id not in self._verified_kernels:
            self._verified_kernels[kernel_id] = _native_ptx_evidence(compiled.asm.get("ptx", ""))
        self.instruction_evidence = self._verified_kernels[kernel_id] | {
            "compiled_kernel_hash": kernel_id, "verified_kernel_count": len(self._verified_kernels)}
        self.last_kernel = compiled
        self.matmul_calls += 1
        return out

    @staticmethod
    def decode(packed, *, dtype=torch.float64):
        return decode_rows(packed, dtype=dtype)

    @staticmethod
    def decode_rows(packed, *, dtype=torch.float64):
        """Explicitly named alias used by representation diagnostic adapters."""
        return decode_rows(packed, dtype=dtype)


def _timed(function, repeats):
    begin, end = torch.cuda.Event(enable_timing=True), torch.cuda.Event(enable_timing=True)
    torch.cuda.synchronize()
    host = time.perf_counter()
    begin.record()
    value = None
    for _ in range(repeats):
        value = function()
    end.record()
    end.synchronize()
    return {"cuda_ms_per_call": begin.elapsed_time(end)/repeats,
            "host_ms_per_call": (time.perf_counter()-host)*1000/repeats}, value


def graph_timed(function, *, calls=32, repeats=5):
    """Warm static-buffer graph latency; capture/CPU launch time stays separate."""
    if calls not in (1, 8, 16, 32, 64) or not 1 <= repeats <= 100:
        raise ValueError("bounded graph calls/repeats required")
    stream = torch.cuda.Stream()
    stream.wait_stream(torch.cuda.current_stream())
    with torch.cuda.stream(stream):
        function()
    torch.cuda.current_stream().wait_stream(stream)
    torch.cuda.synchronize()
    graph = torch.cuda.CUDAGraph()
    capture_started = time.perf_counter()
    with torch.cuda.graph(graph, stream=stream):
        for _ in range(calls):
            function()
    torch.cuda.synchronize()
    capture_ms = (time.perf_counter()-capture_started)*1000
    graph.replay()
    timing, _ = _timed(graph.replay, repeats)
    return {"cuda_ms_per_call": timing["cuda_ms_per_call"]/calls,
            "host_ms_per_call": timing["host_ms_per_call"]/calls,
            "capture_ms": capture_ms, "unrolled_calls": calls, "replays": repeats}


def _save_kernel(kernel, destination):
    destination.mkdir(parents=True, exist_ok=True)
    files = {}
    for name in ("ptx", "cubin", "ttir", "ttgir", "llir"):
        value = kernel.asm.get(name)
        if value is not None:
            path = destination/f"product.{name}"
            path.write_bytes(value if isinstance(value, bytes) else value.encode())
            files[name] = {"file": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    if "cubin" in files and shutil.which("cuobjdump"):
        result = subprocess.run(["cuobjdump", "--dump-sass", files["cubin"]["file"]],
                                capture_output=True, text=True, timeout=30)
        path = destination/"product.sass"
        path.write_text(result.stdout+result.stderr)
        files["sass"] = {"file": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                         "returncode": result.returncode}
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shape", default="128,128,128", help="M,N,K; both operands stored rows")
    parser.add_argument("--format", choices=FORMATS, default="nvfp4")
    parser.add_argument("--seed", type=int, default=1701)
    parser.add_argument("--repeats", type=int, default=20)
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--block-m", type=int, default=128)
    parser.add_argument("--block-n", type=int, default=128)
    parser.add_argument("--block-k", type=int, default=128)
    parser.add_argument("--num-warps", type=int, default=4)
    parser.add_argument("--num-stages", type=int, default=2)
    parser.add_argument("--graph-calls", type=int, choices=(8, 16, 32, 64), default=32)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()
    shape = tuple(map(int, args.shape.split(",")))
    if len(shape) != 3 or min(shape) < 1 or args.repeats < 1 or not 0 <= args.warmup <= 100:
        parser.error("positive M,N,K/repeats and bounded warmup required")
    m, n, k = shape
    if m*n > 2**28 or (m+n)*k > 2**28 or k > 32768:
        parser.error("probe shape exceeds bounded allocation")
    report = {"schema": "triad-native-fp4-probe-v1", "shape_mnk": shape, "seed": args.seed,
              "format": args.format, "argv": vars(args) | {"output": str(args.output), "artifacts": str(args.artifacts)},
              "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "environment": {"python": platform.python_version(), "torch": torch.__version__,
                              "cuda_runtime": torch.version.cuda},
              "calibration": "representation/arithmetic screening only; no operational admission guarantee"}
    started = time.perf_counter()
    try:
        if not torch.cuda.is_available():
            raise RuntimeError("CUDA unavailable; native FP4 execution untested")
        import triton
        report["environment"].update(triton=triton.__version__, gpu=torch.cuda.get_device_name(),
                                     capability=torch.cuda.get_device_capability())
        torch.set_float32_matmul_precision("highest")
        torch.backends.cuda.matmul.allow_tf32 = False
        generator = torch.Generator().manual_seed(args.seed)
        prepared = time.perf_counter()
        ah = torch.randn((m, k), generator=generator)
        bh = torch.randn((n, k), generator=generator)
        report["cpu_rng_ms"] = (time.perf_counter()-prepared)*1000
        transfer = time.perf_counter()
        a, b = ah.cuda(), bh.cuda()
        torch.cuda.synchronize()
        report["transfer_ms"] = (time.perf_counter()-transfer)*1000
        backend = NativeFp4Backend(args.format, block_m=args.block_m, block_n=args.block_n,
                                   block_k=args.block_k, num_warps=args.num_warps, num_stages=args.num_stages)
        cold = time.perf_counter()
        qa, qb = backend.pack_rows(a), backend.pack_rows(b)
        output = backend.matmul(qa, qb)
        torch.cuda.synchronize()
        report["cold_pack_compile_product_ms"] = (time.perf_counter()-cold)*1000
        # cuBLAS initializes lazily: its first use must never be hidden in the
        # measured strict baseline while the native path is already warm.
        strict_output = torch.empty((m, n), dtype=torch.float32, device=a.device)
        cold_strict = time.perf_counter()
        torch.mm(a, b.T, out=strict_output)
        torch.cuda.synchronize()
        report["cold_strict_fp32_initialization_ms"] = (time.perf_counter()-cold_strict)*1000
        for _ in range(args.warmup):
            backend.matmul(qa, qb, out=output)
            backend.pack_rows(a)
            backend.pack_rows(b)
            torch.mm(a, b.T, out=strict_output)
            backend.matmul(qa, backend.pack_rows(b), out=output)
            backend.matmul(backend.pack_rows(a), backend.pack_rows(b), out=output)
        report["packing_a"], _ = _timed(lambda: backend.pack_rows(a), args.repeats)
        report["packing_b"], _ = _timed(lambda: backend.pack_rows(b), args.repeats)
        report["resident_product"], _ = _timed(lambda: backend.matmul(qa, qb, out=output), args.repeats)
        report["packing_and_product"], _ = _timed(
            lambda: backend.matmul(backend.pack_rows(a), backend.pack_rows(b), out=output), args.repeats)
        report["cached_a_packing_b_product"], _ = _timed(
            lambda: backend.matmul(qa, backend.pack_rows(b), out=output), args.repeats)
        report["strict_fp32_product"], _ = _timed(lambda: torch.mm(a, b.T, out=strict_output), args.repeats)
        report["native_product_graph"] = graph_timed(lambda: backend.matmul(qa, qb, out=output), calls=args.graph_calls)
        report["strict_fp32_product_graph"] = graph_timed(lambda: torch.mm(a, b.T, out=strict_output), calls=args.graph_calls)
        # Every element is checked in small probes; large throughput probes
        # explicitly retain only a bounded reference submatrix.
        full_oracle = m*n <= 2**20 and m*n*k <= 2**26
        am, bn = (m, n) if full_oracle else (min(m, 32), min(n, 32))
        ad, bd = backend.decode(subset_rows(qa, am)).cpu(), backend.decode(subset_rows(qb, bn)).cpu()
        ref = ad@bd.T
        observed = output[:am, :bn].cpu().double()
        error = observed-ref
        scale = ad.abs()@bd.abs().T
        bound = (k*2.**-24)/(1-k*2.**-24)*scale
        tolerance = 2e-5 + 8*bound
        report["arithmetic"] = {"oracle": "CPU FP64 dequantized operands", "checked_shape": [am, bn, k],
                                "scope": "exhaustive_full_output" if full_oracle else "bounded_first_rows_submatrix",
                                "max_abs": error.abs().max().item(), "rms": error.square().mean().sqrt().item(),
                                "max_abs_over_sum_abs_products": (error.abs()/scale.clamp_min(1e-300)).max().item(),
                                "gamma_k_bound_max": bound.max().item(),
                                "pass_conservative_fp32_bound": bool((error.abs() <= tolerance).all())}
        ca, cb = pack_rows_cpu(ah[:am], args.format, block_k=args.block_k), pack_rows_cpu(bh[:bn], args.format, block_k=args.block_k)
        report["packing_cpu_agreement"] = {
            "checked_a_rows": am, "checked_b_rows": bn,
            "a_bytes_equal": bool(torch.equal(qa.data[:am].cpu(), ca.data)),
            "b_bytes_equal": bool(torch.equal(qb.data[:bn].cpu(), cb.data)),
            "a_scales_equal": bool(torch.equal(qa.scales[:am].view(torch.uint8).cpu(), ca.scales.view(torch.uint8))),
            "b_scales_equal": bool(torch.equal(qb.scales[:bn].view(torch.uint8).cpu(), cb.scales.view(torch.uint8))),
            "a_globals_equal": bool(torch.equal(qa.row_scales[:am].cpu(), ca.row_scales)),
            "b_globals_equal": bool(torch.equal(qb.row_scales[:bn].cpu(), cb.row_scales))}
        measured_kernel = backend.last_kernel
        measured_instruction_evidence = backend.instruction_evidence
        # Check the previously failing finite-result/opposite-row-exponent
        # boundary on actual GPU arithmetic, outside performance measurements.
        edge_a = torch.full((1, 128), 1e37, dtype=torch.float32, device="cuda")
        edge_b = torch.full((1, 128), 1e-36, dtype=torch.float32, device="cuda")
        edge_qa, edge_qb = backend.pack_rows(edge_a), backend.pack_rows(edge_b)
        edge_y = backend.matmul(edge_qa, edge_qb).cpu().double()
        edge_ref = backend.decode(edge_qa).cpu()@backend.decode(edge_qb).cpu().T
        report["extreme_row_scale_edge"] = {
            "actual": edge_y.item(), "represented_fp64": edge_ref.item(),
            "finite": bool(torch.isfinite(edge_y).all()),
            "instruction_evidence": backend.instruction_evidence,
            "pass": bool(torch.allclose(edge_y, edge_ref, rtol=2e-5, atol=2e-5))}
        report["packing"] = {"a_bytes": qa.nbytes, "b_bytes": qb.nbytes,
                             "a_shape": qa.shape, "b_shape": qb.shape, "a_padded_shape": qa.padded_shape,
                             "b_padded_shape": qb.padded_shape,
                             "diagnostics_columns": ["nonfinite_count", "saturated_count", "nonzero_to_zero_count", "max_abs_input"],
                             "a_diag_sum": qa.diagnostics.sum(dim=0).cpu().tolist(),
                             "b_diag_sum": qb.diagnostics.sum(dim=0).cpu().tolist()}
        report["instruction_evidence"] = measured_instruction_evidence
        report["kernel_artifacts"] = _save_kernel(measured_kernel, args.artifacts)
        report["dense_flop_estimate"] = 2*m*n*k
        report["measured_hardware_counters"] = None
        report["kernel_metadata"] = str(measured_kernel.metadata)
        packing_ok = all(v for name, v in report["packing_cpu_agreement"].items() if name.endswith("equal"))
        report["status"] = ("passed" if report["arithmetic"]["pass_conservative_fp32_bound"] and
                            report["extreme_row_scale_edge"]["pass"] and packing_ok else "failed")
    except Exception as error:
        # This executable is an evidence/reporting boundary. Compilation
        # errors are retained as failures and never trigger another backend.
        report["status"] = "failed"
        report["error"] = f"{type(error).__name__}: {error}"
        report["traceback"] = traceback.format_exc()
    report["complete_probe_seconds"] = time.perf_counter()-started
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+"\n")
    print(json.dumps({"status": report["status"], "output": str(args.output), "error": report.get("error")}))
    return 0 if report["status"] == "passed" else 2


if __name__ == "__main__":
    raise SystemExit(main())

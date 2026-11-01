"""Opt-in CUDA correlation reconstruction with FP64 phase arithmetic.

This fuses only real-product reconstruction, the existing FP64 residual
phasor multiplication, normalization and output layout. Product precision,
quantization, template generation and the receiver model are unchanged.
There is no CPU fallback. CPU helpers below are explicit algebra oracles.
"""
from functools import lru_cache

import torch


def reconstruction_reference(product, residual, reciprocal_variance, *, candidates, taps):
    """Original Torch operation order, useful for paired CPU/GPU validation."""
    frequencies, columns = residual.shape
    parts = product.reshape(frequencies, candidates, taps, 2, columns).double()
    value = torch.complex(parts[:, :, :, 0], parts[:, :, :, 1])
    value = value*residual[:, None, None, :]*reciprocal_variance
    return value.permute(3, 0, 1, 2).contiguous()


@lru_cache(maxsize=1)
def _kernel():
    try:
        import triton
        import triton.language as tl
    except ImportError as error:
        raise RuntimeError("fused reconstruction requires Triton; no CPU fallback") from error

    @triton.jit
    def reconstruct(P, R, SCALE, OUT, N: tl.constexpr, F: tl.constexpr,
                    C: tl.constexpr, J: tl.constexpr,
                    RSF: tl.constexpr, RSN: tl.constexpr, BLOCK: tl.constexpr):
        offset = tl.program_id(0)*BLOCK+tl.arange(0, BLOCK)
        count = N*F*C*J
        valid = offset < count
        column = offset//(F*C*J)
        row = offset % (F*C*J)
        frequency = row//(C*J)
        real = tl.load(P+(2*row)*N+column, valid, other=0.).to(tl.float64)
        imag = tl.load(P+(2*row+1)*N+column, valid, other=0.).to(tl.float64)
        phase_real = tl.load(R+frequency*RSF+column*RSN, valid, other=0.)
        phase_imag = tl.load(R+frequency*RSF+column*RSN+1, valid, other=0.)
        scale = tl.load(SCALE)
        # Keep separate FP64 multiplies/adds; do not replace with FP32 phase
        # arithmetic or enable FMA reassociation. Torch scalar division uses
        # the same FP64 reciprocal multiplication.
        out_real = (real*phase_real-imag*phase_imag)*scale
        out_imag = (real*phase_imag+imag*phase_real)*scale
        tl.store(OUT+2*offset, out_real, valid)
        tl.store(OUT+2*offset+1, out_imag, valid)

    return triton, reconstruct


def reconstruct_correlations(product, residual, reciprocal_variance, *, candidates, taps):
    """[2*F*C*J,N] plus complex128 [F,N] -> complex128 [N,F,C,J]."""
    if product.device.type != "cuda":
        raise RuntimeError("fused reconstruction requires CUDA; no CPU fallback")
    if (product.ndim != 2 or product.dtype not in (torch.float32, torch.float64) or
            not product.is_contiguous() or residual.ndim != 2 or
            residual.dtype != torch.complex128 or residual.device != product.device or
            not isinstance(candidates, int) or candidates < 1 or
            not isinstance(taps, int) or taps < 1):
        raise ValueError("invalid fused reconstruction operands")
    frequencies, columns = residual.shape
    if product.shape != (2*frequencies*candidates*taps, columns):
        raise ValueError("fused reconstruction shapes disagree")
    if (reciprocal_variance.device != product.device or reciprocal_variance.dtype != torch.float64 or
            reciprocal_variance.numel() != 1):
        raise ValueError("normalization must be a resident FP64 scalar")
    result = torch.empty((columns, frequencies, candidates, taps),
                         device=product.device, dtype=torch.complex128)
    phase = torch.view_as_real(residual)
    triton, kernel = _kernel()
    kernel[(triton.cdiv(result.numel(), 256),)](
        product, phase, reciprocal_variance, torch.view_as_real(result),
        N=columns, F=frequencies, C=candidates, J=taps,
        RSF=phase.stride(0), RSN=phase.stride(1), BLOCK=256,
        num_warps=4, enable_fp_fusion=False)
    return result

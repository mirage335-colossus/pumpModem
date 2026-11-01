"""Diagnostic adapter separating operand representation from arithmetic.

Every operand still passes through the actual selected arithmetic packer. Products
are calculated explicitly in CPU FP64. By default they are rounded to the
correlation backend's usual FP32 output; optional FP64 output retains small
diagnostic margins. These runs diagnose quantization only; they are not
native-GPU throughput measurements and do not reproduce native MMA
accumulation error.
"""
from __future__ import annotations

from dataclasses import dataclass

import torch


_MODES = ("strict", "template-only", "observation-only", "both")


@dataclass
class RepresentedRows:
    original: torch.Tensor  # CPU FP64 copy of the FP32 rows presented to packer
    native: object
    nbytes: int


class Fp4RepresentationDiagnostic:
    """Native-packer wrapper for paired T-only/X-only/both diagnostics.

    For full-precision downstream diagnostics, pass this object as ``backend``
    to ``ExhaustiveCorrelationBank`` with ``precision='diagnostic-fp64'`` and
    ``output_dtype='fp64'``. It delegates packing and decode to the actual
    native backend while replacing only the matrix product. The bank's usual
    half-aligned complex rows and actual sampled IQ windows are retained.
    Cache accounting charges original rows as well as packed bytes. The
    FP32-output mode is also available where the bank requires it, but it is
    a diagnostic substitution and does not execute native FP4 multiplication.
    """

    def __init__(self, native_backend, mode="both", *, output_dtype="fp32"):
        if mode not in _MODES:
            raise ValueError(f"mode must be one of {_MODES}")
        if output_dtype not in ("fp32", "fp64"):
            raise ValueError("output_dtype must be fp32 or fp64")
        self.native_backend = native_backend
        self.mode = mode
        self.output_dtype = output_dtype
        self._torch_output_dtype = torch.float32 if output_dtype == "fp32" else torch.float64
        self.group_size = native_backend.group_size
        self.packs = 0
        self.products = 0
        self.last_product_diagnostics = None
        self.quantization_totals = {"input_rows": 0, "input_values": 0,
                                    "input_packed_bytes": 0}

    def pack_rows(self, rows):
        if rows.dtype != torch.float32 or rows.ndim != 2 or not rows.is_contiguous():
            raise ValueError("diagnostic pack input must be contiguous FP32 rows")
        native = self.native_backend.pack_rows(rows)
        original = rows.detach().to(device="cpu", dtype=torch.float64).contiguous()
        size = int(native.nbytes) + original.numel()*original.element_size()
        self.packs += 1
        self.quantization_totals["input_rows"] += int(rows.shape[0])
        self.quantization_totals["input_values"] += int(rows.numel())
        self.quantization_totals["input_packed_bytes"] += int(native.nbytes)
        return RepresentedRows(original, native, size)

    @staticmethod
    def packed_nbytes(packed):
        return packed.nbytes

    def decode_rows(self, packed):
        return self.native_backend.decode_rows(packed.native, dtype=torch.float64)

    def _original(self, packed):
        return packed.original

    def _represented_cpu(self, packed):
        return self.native_backend.decode_rows(packed.native, dtype=torch.float64).detach().cpu()

    def product_variants(self, a, b, *, max_elements=1_000_000,
                         max_multiply_terms=64_000_000, max_input_values=16_000_000):
        """Return four FP64 representation products and actual native result.

        This exhaustive diagnostic is deliberately bounded because products
        live on the CPU. It uses exactly the operands produced by the actual
        GPU packer, including its scale formats and row scales.
        """
        m, k = a.original.shape
        n, kb = b.original.shape
        if (k != kb or m*n > max_elements or m*n*k > max_multiply_terms or
                (m+n)*k > max_input_values):
            raise ValueError("representation product exceeds explicit element limit")
        qa, qb = self._represented_cpu(a), self._represented_cpu(b)
        original_a, original_b = a.original, b.original
        # The native result is retained separately so representation and MMA
        # accumulation differences are not conflated.
        native_result = self.native_backend.matmul(a.native, b.native).detach().cpu().double()
        return dict(reference=original_a @ original_b.T,
                    template_only=qa @ original_b.T,
                    observation_only=original_a @ qb.T,
                    both=qa @ qb.T,
                    native=native_result,
                    native_minus_represented=(native_result - qa @ qb.T),
                    semantics="four CPU FP64 products preserve representation effects; native result and native-minus-both use the selected arithmetic backend output")

    def matmul(self, a, b):
        if not isinstance(a, RepresentedRows) or not isinstance(b, RepresentedRows):
            raise TypeError("representation diagnostics require packs from this adapter")
        original_a, original_b = a.original, b.original
        if self.mode == "strict":
            product = original_a @ original_b.T
        elif self.mode == "template-only":
            product = self._represented_cpu(a) @ original_b.T
        elif self.mode == "observation-only":
            product = original_a @ self._represented_cpu(b).T
        else:
            product = self._represented_cpu(a) @ self._represented_cpu(b).T
        self.products += 1
        self.last_product_diagnostics = dict(
            mode=self.mode, product_elements=int(product.numel()),
            arithmetic="CPU FP64 matrix multiply",
            output_dtype=self.output_dtype,
            output_rounding=("rounded to FP32 at bank interface" if self.output_dtype == "fp32"
                             else "FP64 product/correlation output; downstream dtype is caller-selected"),
            native_mma_executed=False)
        return product.to(dtype=self._torch_output_dtype, device=a.native.device)

    def diagnostics(self):
        return dict(mode=self.mode, pack_calls=self.packs,
                    product_calls=self.products,
                    packed_input_totals=dict(self.quantization_totals),
                    output_dtype=self.output_dtype,
                    last_product=self.last_product_diagnostics,
                    native_mma_executed=False,
                    representation_backend=(self.native_backend.describe()
                        if hasattr(self.native_backend, "describe") else
                        dict(format=getattr(self.native_backend, "format", "unspecified"))),
                    purpose="representation-only diagnostic; exclude from throughput")


# Backward-compatible name for the original FP4 study. The adapter operates on
# actual packed/dequantized operands and is equally applicable to other formats.
RepresentationDiagnostic = Fp4RepresentationDiagnostic

"""CPU representation diagnostics use the production FP4 pack/decode logic."""
from pathlib import Path
import sys
import unittest

import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from triad_fp4_native import decode_rows, pack_rows_cpu
from triad_fp4_representation import Fp4RepresentationDiagnostic


class CpuPackingForDiagnostics:
    """Explicit CPU oracle for tests; never claims a native execution path."""
    group_size = 16

    def pack_rows(self, rows):
        return pack_rows_cpu(rows)

    def packed_nbytes(self, packed):
        return packed.nbytes

    def decode_rows(self, packed, *, dtype=torch.float64):
        return decode_rows(packed, dtype=dtype)

    def matmul(self, a, b):
        return decode_rows(a) @ decode_rows(b).T


class RepresentationDiagnosticTests(unittest.TestCase):
    def setUp(self):
        self.backend = Fp4RepresentationDiagnostic(CpuPackingForDiagnostics(), "both")
        torch.manual_seed(321)
        self.a = self.backend.pack_rows(torch.randn((8, 32), dtype=torch.float32).contiguous())
        self.b = self.backend.pack_rows(torch.randn((6, 32), dtype=torch.float32).contiguous())

    def test_accounting_covers_original_and_packed_rows(self):
        self.assertEqual(self.backend.packed_nbytes(self.a),
                         self.a.native.nbytes+self.a.original.numel()*8)
        self.assertEqual(self.backend.packed_nbytes(self.b),
                         self.b.native.nbytes+self.b.original.numel()*8)
        self.assertEqual(self.backend.quantization_totals["input_rows"], 14)

    def test_modes_separate_template_and_sample_quantization(self):
        original_a, original_b = self.a.original, self.b.original
        represented_a, represented_b = self.backend._represented_cpu(self.a), self.backend._represented_cpu(self.b)
        expected = {
            "strict": original_a@original_b.T,
            "template-only": represented_a@original_b.T,
            "observation-only": original_a@represented_b.T,
            "both": represented_a@represented_b.T,
        }
        for mode, want in expected.items():
            diagnostic = Fp4RepresentationDiagnostic(CpuPackingForDiagnostics(), mode)
            pa, pb = diagnostic.pack_rows(original_a.float().contiguous()), diagnostic.pack_rows(original_b.float().contiguous())
            actual = diagnostic.matmul(pa, pb)
            torch.testing.assert_close(actual.double(), want.float().double(), rtol=0, atol=0)
            self.assertFalse(diagnostic.diagnostics()["native_mma_executed"])

    def test_product_variants_compare_actual_packed_representation(self):
        products = self.backend.product_variants(self.a, self.b)
        qa, qb = self.backend._represented_cpu(self.a), self.backend._represented_cpu(self.b)
        torch.testing.assert_close(products["reference"], self.a.original@self.b.original.T)
        torch.testing.assert_close(products["template_only"], qa@self.b.original.T)
        torch.testing.assert_close(products["observation_only"], self.a.original@qb.T)
        torch.testing.assert_close(products["both"], qa@qb.T)
        self.assertEqual(products["native"].shape, (8, 6))
        with self.assertRaises(ValueError):
            self.backend.product_variants(self.a, self.b, max_elements=1)
        with self.assertRaises(ValueError):
            self.backend.product_variants(self.a, self.b, max_multiply_terms=1)

    def test_fp64_output_preserves_small_product_margin(self):
        # The leading product is one; the second is below half an FP32 ulp.
        # Input rows are still the same FP32 values sent to the actual packer.
        a = torch.tensor([[1.e20, 1.]], dtype=torch.float32)
        b = torch.tensor([[1.e-20, 2.**-25]], dtype=torch.float32)
        expected = a.double() @ b.double().T
        fp32_adapter = Fp4RepresentationDiagnostic(CpuPackingForDiagnostics(), "strict")
        fp64_adapter = Fp4RepresentationDiagnostic(CpuPackingForDiagnostics(), "strict", output_dtype="fp64")
        af, bf = fp32_adapter.pack_rows(a.contiguous()), fp32_adapter.pack_rows(b.contiguous())
        a64, b64 = fp64_adapter.pack_rows(a.contiguous()), fp64_adapter.pack_rows(b.contiguous())
        rounded = fp32_adapter.matmul(af, bf)
        retained = fp64_adapter.matmul(a64, b64)
        self.assertEqual(rounded.dtype, torch.float32)
        self.assertEqual(retained.dtype, torch.float64)
        self.assertEqual(rounded.item(), 1.)
        self.assertEqual(retained.item(), expected.item())
        self.assertGreater(retained.item()-rounded.item(), 0.)
        self.assertEqual(fp32_adapter.diagnostics()["output_dtype"], "fp32")
        self.assertEqual(fp64_adapter.diagnostics()["output_dtype"], "fp64")


if __name__ == "__main__":
    unittest.main()

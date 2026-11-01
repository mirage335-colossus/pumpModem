"""Representation and dispatch guards; actual CUDA instruction tests are probes."""
from pathlib import Path
import sys
import unittest

import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from triad_fp4_native import (NativeFp4Backend, _codes_cpu, _native_ptx_evidence,
                              _restore_rows_cpu, decode_rows, pack_rows_cpu)


class Fp4RepresentationTests(unittest.TestCase):
    def test_nibble_order_signed_levels_and_even_ties(self):
        levels = torch.tensor([[0., .5, 1., 1.5, 2., 3., 4., 6.,
                                -.5, -1., -1.5, -2., -3., -4., -6., -0.]])
        expected = torch.tensor([[0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15, 8]], dtype=torch.uint8)
        self.assertTrue(torch.equal(_codes_cpu(levels), expected))
        mids = torch.tensor([[.25, .75, 1.25, 1.75, 2.5, 3.5, 5.]])
        self.assertEqual(_codes_cpu(mids).tolist(), [[0, 2, 2, 4, 4, 6, 6]])

    def test_both_formats_padding_no_saturation_and_range(self):
        generator = torch.Generator().manual_seed(111)
        x = torch.randn((7, 137), generator=generator)
        x *= torch.tensor([1e-30, 1e-12, .1, 1., 1e6, 1e20, 1e30])[:, None]
        x[0] = 0.
        for format in ("nvfp4", "mxfp4"):
            p = pack_rows_cpu(x, format)
            decoded = decode_rows(p)
            self.assertEqual(p.shape, (7, 137))
            self.assertEqual(p.padded_shape, (7, 256))
            self.assertEqual(p.data.shape, (7, 128))
            self.assertEqual(decoded.shape, x.shape)
            self.assertTrue(bool(torch.isfinite(decoded).all()))
            self.assertTrue(bool((p.diagnostics[:, :2] == 0).all()))
            self.assertTrue(bool((decoded[0] == 0).all()))
            # FP64 prevents the diagnostic itself overflowing on the range fixture.
            relative = (decoded[1:].double()-x[1:].double()).square().sum(1)/x[1:].double().square().sum(1)
            self.assertLess(float(relative.max()), .03)
            self.assertGreater(p.nbytes, p.data.numel())

    def test_zero_padding_decodes_exactly_zero(self):
        x = torch.tensor([[1., -2., .1]])
        p = pack_rows_cpu(x)
        self.assertTrue(bool((p.data[:, 2:] == 0).all()))
        self.assertTrue(torch.equal(decode_rows(p), decode_rows(p)[:, :3]))

    def test_invalid_and_native_cpu_guard(self):
        for x in (torch.ones(2, 3, dtype=torch.float64), torch.ones(0, 3), torch.ones(2, 3).T):
            with self.assertRaises(ValueError):
                pack_rows_cpu(x)
        with self.assertRaises(ValueError):
            pack_rows_cpu(torch.tensor([[float("nan")]]))
        with self.assertRaisesRegex(RuntimeError, "requires CUDA"):
            NativeFp4Backend().pack_rows(torch.ones(2, 3))
        packed = pack_rows_cpu(torch.ones(2, 3))
        backend = NativeFp4Backend()
        self.assertTrue(torch.equal(backend.decode_rows(packed), backend.decode(packed)))

    def test_opposite_extreme_scales_preserve_finite_product(self):
        for format in ("nvfp4", "mxfp4"):
            a = pack_rows_cpu(torch.full((1, 128), 1e37), format)
            b = pack_rows_cpu(torch.full((1, 128), 1e-36), format)
            # Remove globals while computing the native normalized product.
            acc = ((decode_rows(a)/a.row_scales.double()[:, None]) @
                   (decode_rows(b)/b.row_scales.double()[:, None]).T).float()
            old = (acc*a.row_scales[:, None])*b.row_scales[None, :]
            self.assertTrue(bool(torch.isinf(old).all()))
            ref = decode_rows(a)@decode_rows(b).T
            restored = _restore_rows_cpu(acc, a.row_scales, b.row_scales)
            self.assertTrue(bool(torch.isfinite(restored).all()))
            torch.testing.assert_close(restored.double(), ref, rtol=1e-7, atol=0)
            maximum = pack_rows_cpu(torch.full((1, 128), torch.finfo(torch.float32).max), format)
            self.assertTrue(bool(torch.isfinite(decode_rows(maximum)).all()))
        for acc, scale, expected in ((2.**-100, 2.**100, 2.**100),
                                     (2.**100, 2.**-100, 2.**-100),
                                     (2.**-100, 2.**-20, 2.**-140)):
            actual = _restore_rows_cpu(torch.tensor([[acc]]), torch.tensor([scale]), torch.tensor([scale]))
            self.assertEqual(actual.item(), expected)
        self.assertTrue(bool(torch.isinf(_restore_rows_cpu(torch.ones(1, 1), torch.tensor([2.**100]),
                                                         torch.tensor([2.**100]))).all()))

    def test_native_instruction_guard_rejects_emulation_and_sparse(self):
        for ptx in ("mma.sync.aligned.f32.bf16.bf16.f32;",
                    "mma.sp.sync.aligned.block_scale.f32.e2m1.e2m1.f32;", ""):
            with self.assertRaises(RuntimeError):
                _native_ptx_evidence(ptx)
        evidence = _native_ptx_evidence("mma.sync.aligned.kind::mxf4nvf4.block_scale.f32.e2m1.e2m1.f32;")
        self.assertTrue(evidence["native_ptx_verified"])
        self.assertFalse(evidence["sparse_instruction"])


if __name__ == "__main__":
    unittest.main()

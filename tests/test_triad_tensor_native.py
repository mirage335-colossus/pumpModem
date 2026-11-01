"""Consumer-format representation/dispatch checks; CUDA execution stays explicit."""
from pathlib import Path
import json
import sys
import unittest

import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from triad_tensor_native import (FORMATS, TensorBackend, _ldexp_f32, _small_product_metrics,
                                 _fp8_preround_cpu, _native_ptx_evidence, capability, decode_rows,
                                 pack_rows_cpu)


class TensorRepresentationTests(unittest.TestCase):
    def test_supported_generation_policy_distinguishes_software_and_hardware(self):
        turing = capability("fp16", (7, 5))
        self.assertTrue(turing["supported"])
        self.assertEqual(turing["implementation"], "torch")
        self.assertFalse(turing["executed_instructions_verified"])
        self.assertFalse(capability("fp16", (7, 5), "triton")["supported"])
        self.assertFalse(capability("bf16", (7, 5))["hardware_format_supported"])
        self.assertFalse(capability("fp8-e4m3", (8, 6))["supported"])
        self.assertTrue(capability("tf32", (8, 6))["supported"])
        self.assertTrue(capability("fp8-e4m3", (8, 9))["supported"])
        self.assertTrue(capability("int8", (8, 9))["supported"])
        self.assertFalse(capability("tf32", (8, 9), "torch")["supported"])

    def test_scale_padding_and_represented_dynamic_range(self):
        generator = torch.Generator().manual_seed(82171)
        source = torch.randn((8, 137), generator=generator)
        source *= torch.tensor([1e-37, 1e-20, 1e-5, 1., 1e8, 1e20, 1e37, 1.])[:, None]
        source[-1] = 0.
        bounds = {"fp16": 1e-6, "bf16": 1e-4, "tf32": 1e-6,
                  "fp8-e4m3": .003, "fp8-e5m2": .012, "int8": .002}
        for format in FORMATS:
            with self.subTest(format=format):
                packed = pack_rows_cpu(source, format)
                represented = decode_rows(packed)
                self.assertEqual(packed.padded_shape, (8, 160))
                self.assertTrue(bool(torch.isfinite(represented).all()))
                self.assertTrue(bool((packed.data[:, 137:].float() == 0).all()))
                self.assertTrue(bool((packed.diagnostics[:, :2] == 0).all()))
                self.assertTrue(bool((represented[-1] == 0).all()))
                error = (represented[:-1]-source[:-1].double()).square().sum(1)/source[:-1].double().square().sum(1)
                self.assertLess(float(error.max()), bounds[format])
                self.assertEqual(packed.nbytes, TensorBackend(format).estimated_packed_nbytes(8, 137))

    def test_tiny_subnormal_row_and_opposite_scales(self):
        tiny = torch.tensor([[torch.finfo(torch.float32).tiny*torch.finfo(torch.float32).eps]])
        for format in FORMATS:
            self.assertEqual(decode_rows(pack_rows_cpu(tiny, format)).item(), tiny.item())
            a = pack_rows_cpu(torch.full((1, 128), 1e37), format)
            b = pack_rows_cpu(torch.full((1, 128), 1e-36), format)
            normalized = (a.data.double()@b.data.double().T).float()
            restored = _ldexp_f32(normalized, a.exponents[:, None]+b.exponents[None, :])
            reference = decode_rows(a)@decode_rows(b).T
            self.assertTrue(bool(torch.isfinite(restored).all()))
            torch.testing.assert_close(restored.double(), reference, rtol=2e-7, atol=0)

    def test_tf32_explicit_round_to_nearest_even(self):
        # Target spacing at1 is2^-10. Ties choose an even low retained bit.
        source = torch.tensor([[1.+2.**-11, 1.+3*2.**-11, -1.-2.**-11, -1.-3*2.**-11]])
        represented = decode_rows(pack_rows_cpu(source, "tf32"))
        expected = torch.tensor([[1., 1.+2.**-9, -1., -1.-2.**-9]], dtype=torch.float64)
        self.assertTrue(torch.equal(represented, expected))

    def test_staged_normalization_declares_subnormal_double_rounding(self):
        source = torch.tensor([0x3c000005], dtype=torch.int32).view(torch.float32)
        exponent = torch.tensor([-122], dtype=torch.int32)
        staged = _ldexp_f32(source, exponent)
        once = torch.ldexp(source.double(), exponent).float()
        self.assertNotEqual(staged.view(torch.int32).item(), once.view(torch.int32).item())
        self.assertEqual(abs(staged.view(torch.int32).item()-once.view(torch.int32).item()), 1)
        metadata = TensorBackend().contract_metadata()
        self.assertFalse(metadata["normalization_single_ldexp_equivalent"])
        self.assertIn("FP64", metadata["product_restoration_arithmetic"])

    def test_fp8_preround_preserves_direct_rne_at_every_midpoint(self):
        for format, dtype in (("fp8-e4m3", torch.float8_e4m3fn), ("fp8-e5m2", torch.float8_e5m2)):
            levels = torch.arange(256, dtype=torch.uint8).view(dtype).float()
            levels = levels[torch.isfinite(levels) & (levels >= 0)].sort().values.unique()
            midpoint = (levels[:-1]+levels[1:])/2
            below = torch.nextafter(midpoint, torch.full_like(midpoint, -float("inf")))
            above = torch.nextafter(midpoint, torch.full_like(midpoint, float("inf")))
            x = torch.cat((midpoint, below, above, -midpoint, -below, -above, torch.tensor([0., -0.])))
            expected = x.to(dtype).view(torch.uint8)
            actual = _fp8_preround_cpu(x, format).to(dtype).view(torch.uint8)
            self.assertTrue(torch.equal(actual, expected), format)

    def test_partial_accumulation_contract_and_guards(self):
        metadata = TensorBackend("fp8-e4m3", accumulation="partial-fp32").contract_metadata()
        self.assertEqual(metadata["accumulation_algorithm"], "partial-fp32")
        self.assertEqual(metadata["partial_sum_k"], 32)
        for kwargs in ({"format": "int8"}, {"block_k": 64}, {"implementation": "torch"}):
            with self.assertRaisesRegex(ValueError, "partial-fp32"):
                TensorBackend(accumulation="partial-fp32", **kwargs)

    def test_small_selector_separates_representation_and_arithmetic_zeroing(self):
        a = torch.tensor([[1., 2.**-20, 2.**-30, 2.**-127, 2.**-149]])
        b = torch.zeros((4, 5))
        b[:, 1:] = torch.eye(4)
        original = a.double()@b.double().T
        represented = decode_rows(pack_rows_cpu(a, "fp16"))@decode_rows(pack_rows_cpu(b, "fp16")).T
        actual = represented.float().double()
        metrics = _small_product_metrics(original, represented, actual)
        self.assertEqual(metrics["representation_zeroed_nonzero_products"], 3)
        self.assertEqual(metrics["arithmetic_zeroed_nonzero_represented_products"], 0)
        self.assertTrue(metrics["exact_fp32_rounding_pass"])
        # An implementation which flushes a retained tiny result is detected
        # separately, without dividing its error by the unselected leading1.
        actual[0, 0] = 0.
        metrics = _small_product_metrics(original, represented, actual)
        self.assertEqual(metrics["arithmetic_zeroed_nonzero_represented_products"], 1)
        self.assertEqual(metrics["arithmetic_zeroed_nonzero_fp32_representable_products"], 1)
        self.assertFalse(metrics["exact_fp32_rounding_pass"])
        self.assertEqual(metrics["relative_arithmetic_error_per_product"][0][0], 1.)

    def test_input_guards_immutable_contract_and_cpu_native_rejection(self):
        for source in (torch.ones((2, 3), dtype=torch.float64), torch.ones((3, 2)).T,
                       torch.ones((0, 3)), torch.ones((2, 3), requires_grad=True)):
            with self.assertRaises(ValueError):
                pack_rows_cpu(source)
        with self.assertRaisesRegex(ValueError, "finite"):
            pack_rows_cpu(torch.tensor([[float("nan")]]))
        backend = TensorBackend("fp16")
        with self.assertRaisesRegex(AttributeError, "immutable"):
            backend.format = "bf16"
        with self.assertRaisesRegex(RuntimeError, "requires CUDA"):
            backend.pack_rows(torch.ones((2, 3)))
        meta = backend.contract_metadata()
        self.assertEqual(meta["multiplication"], "fp16")
        self.assertEqual(meta["accumulation"], "fp32")
        self.assertFalse(backend.runtime_metadata()["native_ptx_verified"])

    def test_torch_scratch_bound_covers_fp64_restoration_allocations(self):
        m, n, k = 7, 5, 137
        # Excluding returned FP32 output: FP32 accumulator(4), broadcast
        # exponent sum(4), converted FP64 accumulator(8), FP64 scaled output(8).
        minimum_elementwise_bytes = m*n*(4+4+8+8)
        library_reserve = 64*1024*1024
        bound = TensorBackend(implementation="torch").estimated_matmul_scratch_nbytes(m, n, k)
        self.assertGreaterEqual(bound, minimum_elementwise_bytes+library_reserve)
        self.assertEqual(TensorBackend(implementation="triton").estimated_matmul_scratch_nbytes(m, n, k), 0)

    def test_dense_instruction_guard_rejects_wrong_precision_and_sparse(self):
        for source in ("", "fma.rn.f32;", "mma.sp.sync.aligned.f32.f16.f16.f32;",
                       "mma.sync.aligned.f16.f16.f16.f16;"):
            with self.assertRaises(RuntimeError):
                _native_ptx_evidence(source, "fp16")
        for format, suffix in (("fp16", ".f32.f16.f16.f32"), ("bf16", ".f32.bf16.bf16.f32"),
                               ("tf32", ".f32.tf32.tf32.f32"), ("fp8-e4m3", ".f32.e4m3.e4m3.f32"),
                               ("fp8-e5m2", ".f32.e5m2.e5m2.f32"), ("int8", ".s32.s8.s8.s32")):
            result = _native_ptx_evidence("mma.sync.aligned.m16n8k32.row.col"+suffix+";", format)
            self.assertTrue(result["native_ptx_verified"])


@unittest.skipUnless(torch.cuda.is_available(), "actual CUDA tensor execution requires a GPU")
class TensorCudaTests(unittest.TestCase):
    def test_fp8_short_partial_preserves_constructed_small_contributions(self):
        cc = torch.cuda.get_device_capability()
        for format in ("fp8-e4m3", "fp8-e5m2"):
            if not capability(format, cc)["supported"]:
                continue
            with self.subTest(format=format):
                a = torch.full((1, 4096), .125, device="cuda")
                b = torch.full_like(a, .125)
                a[:, 1:32] = 0.
                b[:, 1:32] = 0.
                a[:, 0] = 256.
                b[:, 0] = 256.
                backend = TensorBackend(format, implementation="triton", pack_implementation="triton",
                                        accumulation="partial-fp32")
                pa, pb = backend.pack_rows(a), backend.pack_rows(b)
                reference = decode_rows(pa).cpu()@decode_rows(pb).cpu().T
                self.assertEqual(reference.item(), 65599.5)
                actual = backend.matmul(pa, pb).cpu().double()
                self.assertTrue(torch.equal(actual, reference))

    def test_optin_fused_pack_is_bit_identical_to_torch_policy(self):
        if torch.cuda.get_device_capability() < (8, 0):
            self.skipTest("fused pack explicitly requires SM80+")
        generator = torch.Generator().manual_seed(178231)
        source = torch.randn((8, 137), generator=generator)
        source[1] *= 1e37
        source[2] *= 1e-37
        source[3] = 0.
        source[3, 0] = -0.
        source[4] = 0.
        source[4, :9] = torch.tensor([1.]+[2.**e for e in (-12, -20, -24, -30, -120, -127, -140, -149)])
        source[5] = torch.finfo(torch.float32).tiny*torch.finfo(torch.float32).eps
        source[6, :4] = torch.tensor([float("nan"), float("inf"), -float("inf"), -0.])
        source[7] = 0.
        source[7, 0] = 2.**122
        # The latter value crosses a TF32 representation boundary if staged
        # normalization is silently replaced by single-rounding scaling.
        source[7, 1:3] = torch.tensor([0x3c000005, 0x3c008005], dtype=torch.int32).view(torch.float32)
        source = source.cuda()
        for format in FORMATS:
            if not capability(format, torch.cuda.get_device_capability())["supported"]:
                continue
            with self.subTest(format=format):
                reference = TensorBackend(format, pack_implementation="torch").pack_rows(source)
                fused = TensorBackend(format, pack_implementation="triton").pack_rows(source)
                ref_bytes = reference.data.view(torch.uint8).cpu()
                fused_bytes = fused.data.view(torch.uint8).cpu()
                mismatch = (ref_bytes != fused_bytes).nonzero()
                normalized = _ldexp_f32(source, -reference.exponents[:, None]).cpu()
                source_cpu = source.cpu()
                details = []
                for row, byte_col in mismatch[:12].tolist():
                    col = byte_col//reference.data.element_size()
                    details.append({"row": row, "column": col, "byte_column": byte_col,
                                    "source": float(source_cpu[row, col]) if col < source.shape[1] else "padding",
                                    "torch_normalized": float(normalized[row, col]) if col < source.shape[1] else 0.,
                                    "reference_byte": int(ref_bytes[row, byte_col]),
                                    "fused_byte": int(fused_bytes[row, byte_col]),
                                    "reference_encoded_value": float(reference.data[row, col].float().cpu()),
                                    "fused_encoded_value": float(fused.data[row, col].float().cpu()),
                                    "reference_exponent": int(reference.exponents[row].cpu()),
                                    "fused_exponent": int(fused.exponents[row].cpu())})
                message = json.dumps({"format": format, "data_byte_mismatch_count": len(mismatch),
                                      "first_data_mismatches": details,
                                      "reference_exponents": reference.exponents.cpu().tolist(),
                                      "fused_exponents": fused.exponents.cpu().tolist(),
                                      "reference_diagnostics": reference.diagnostics.cpu().tolist(),
                                      "fused_diagnostics": fused.diagnostics.cpu().tolist()})
                self.assertEqual(len(mismatch), 0, message)
                self.assertTrue(torch.equal(fused.exponents, reference.exponents), message)
                self.assertTrue(torch.equal(fused.diagnostics, reference.diagnostics), message)
                self.assertEqual(fused.nbytes, reference.nbytes)

    def test_available_formats_match_independent_represented_oracle(self):
        torch.manual_seed(98371)
        cc = torch.cuda.get_device_capability()
        previous = {name: getattr(torch.backends.cuda.matmul, name) for name in
                    ("allow_fp16_reduced_precision_reduction", "allow_bf16_reduced_precision_reduction")}
        try:
            for name in previous:
                setattr(torch.backends.cuda.matmul, name, False)
            for format in FORMATS:
                if not capability(format, cc)["supported"]:
                    continue
                with self.subTest(format=format):
                    backend = TensorBackend(format)
                    a = torch.randn((7, 137), device="cuda")
                    b = torch.randn((5, 137), device="cuda")
                    pa, pb = backend.pack_rows(a), backend.pack_rows(b)
                    represented = decode_rows(pa).cpu()@decode_rows(pb).cpu().T
                    actual = backend.matmul(pa, pb).cpu().double()
                    absolute_products = decode_rows(pa).cpu().abs()@decode_rows(pb).cpu().abs().T
                    bound = absolute_products*(4*torch.finfo(torch.float32).eps*137)
                    self.assertTrue(bool(((actual-represented).abs() <= bound+1e-30).all()))
        finally:
            for name, value in previous.items():
                setattr(torch.backends.cuda.matmul, name, value)


if __name__ == "__main__":
    unittest.main()

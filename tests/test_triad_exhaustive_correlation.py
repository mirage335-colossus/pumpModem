#!/usr/bin/env python3
"""Small exhaustive sampled-bank oracle checks; no native GPU claim."""
from pathlib import Path
import sys
import unittest

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from triad_exhaustive_correlation import (ExhaustiveCorrelationBank,
    complex_template_rows, direct_fp64_reference, observation_rows,
    representation_diagnostics)

CUDA_CHECKS = "--cuda" in sys.argv


def fixture():
    rng = np.random.default_rng(817)
    # Four public bit/epoch/clock candidates, not just one winner per bit.
    sample = np.arange(19, dtype=np.float64)
    basis = rng.normal(size=(4, 19, 2))+1j*rng.normal(size=(4, 19, 2))
    # Fractional arrival/clock phases are already in the actual sampled public
    # template; no integer-grid or zero-clock shortcut may replace them.
    for candidate, clock in enumerate((-.7, .1, .4, 1.1)):
        basis[candidate] *= np.exp(1j*(sample[:, None]+.375)*(1+clock*1e-6)*.319)
    # Preserve a zero-energy interval and complex, nonorthogonal coupled taps.
    basis[:, 7:14] = 0
    basis[:, :, 1] += (.23+.41j)*basis[:, :, 0]
    observations = rng.normal(size=(3, 24))+1j*rng.normal(size=(3, 24))
    return basis, observations


class RecordingBackend:
    """Explicit test double for backend routing, never a native FP4 fallback."""
    def __init__(self):
        self.group_size = 1
        self.pack_calls = 0
        self.products = 0

    def pack_rows(self, value):
        self.pack_calls += 1
        assert value.dtype == torch.float32
        return value.clone()

    def packed_nbytes(self, value):
        return value.numel()*value.element_size()

    def matmul(self, a, b):
        self.products += 1
        return a@b.T


class ExhaustiveChecks(unittest.TestCase):
    def test_real_complex_signs(self):
        template = torch.tensor([[1+2j, 3-4j], [2-1j, -3+2j]], dtype=torch.complex128)
        signal = torch.tensor([[2+3j, -4+1j], [1-1j, 3+2j]], dtype=torch.complex128)
        rows = complex_template_rows(template).reshape(4, 4)
        product = (rows@observation_rows(signal).T).reshape(2, 2, 2)
        actual = torch.complex(product[:, 0], product[:, 1])
        torch.testing.assert_close(actual, template.conj()@signal.T, rtol=0, atol=0)

    def test_every_axis_against_direct(self):
        basis, observations = fixture()
        bank = ExhaustiveCorrelationBank(basis, 13.5, [-.213, 0., .217], [0, 2, 5], 7,
                                         candidate_tile=3, frequency_tile=2, column_tile=4)
        references = direct_fp64_reference(basis, observations, 13.5, [-.213, 0., .217], [0, 2, 5], 7,
                                          2.3, time_origin_seconds=172800.125)
        blocks = list(bank.iter_blocks(observations, 2.3, time_origin_seconds=172800.125))
        self.assertEqual(bank.hypotheses, 36)
        self.assertEqual(len(blocks), 3)
        for block, (r, gram) in zip(blocks, references):
            np.testing.assert_allclose(block.r.numpy(), r, rtol=3e-14, atol=2e-14)
            np.testing.assert_allclose(block.gram.numpy(), gram, rtol=3e-15, atol=3e-15)
            np.testing.assert_allclose(block.gram.numpy(), block.gram.numpy().conj().swapaxes(-1, -2), atol=1e-14)
            np.testing.assert_allclose(block.centers_seconds.numpy(),
                172800.125+sum(block.interval)/(2*13.5)+np.array([0, 2, 5])/13.5)
        self.assertEqual(blocks[0].dt, 0.)
        self.assertAlmostEqual(blocks[1].dt, 7/13.5)
        self.assertAlmostEqual(blocks[2].dt, 6/13.5)
        self.assertEqual(blocks[-1].interval, (14, 19))
        np.testing.assert_array_equal(blocks[1].r.numpy(), 0)
        np.testing.assert_array_equal(blocks[1].gram.numpy(), 0)
        # This off-diagonal is complex: dropping its imaginary part is wrong.
        self.assertGreater(abs(blocks[0].gram[0, 0, 1].imag), .1)

    def test_tiling_and_cache_reuse(self):
        basis, observations = fixture()
        bank = ExhaustiveCorrelationBank(basis, 17., [-.1, .2], [0, 5], 7,
                                         candidate_tile=2, frequency_tile=1, column_tile=4)
        first = list(bank.iter_blocks(observations))
        cold = bank.accounting()
        # 3 blocks * ceil(3 trials*2 lags / 4 columns), independent of CFO/C.
        self.assertEqual(cold["b_pack_calls"], 6)
        self.assertEqual(cold["a_pack_calls"], 12)
        self.assertEqual(cold["matmul_calls"], 24)
        self.assertEqual(cold["gram_compute_calls"], 3)
        self.assertEqual(cold["geometry_prepare_calls"], 2)
        bank.reset_counters()
        warm = list(bank.iter_blocks(observations))
        counters = bank.accounting()
        self.assertEqual(counters["a_pack_calls"], 0)
        self.assertEqual(counters["a_cache_hits"], 24)
        self.assertEqual(counters["b_pack_calls"], 6)
        self.assertEqual(counters["gram_cache_hits"], 3)
        self.assertEqual(counters["gram_compute_calls"], 0)
        self.assertEqual(counters["gram_h2d_calls"], 0)
        self.assertEqual(counters["geometry_prepare_calls"], 0)
        self.assertEqual(counters["geometry_cache_hits"], 6)
        for a, b in zip(first, warm):
            torch.testing.assert_close(a.r, b.r, rtol=0, atol=0)
        self.assertLessEqual(counters["template_cache_bytes"], bank.template_cache_bytes)
        bank.clear_cache()
        self.assertEqual(bank.accounting()["template_cache_bytes"], 0)
        self.assertEqual(bank.accounting()["gram_cache_bytes"], 0)
        self.assertEqual(bank.accounting()["geometry_cache_bytes"], 0)

    def test_gram_cache_is_unnormalized_and_bounded(self):
        basis, observations = fixture()
        bank = ExhaustiveCorrelationBank(basis, 17., [0.], [0], 7, gram_cache_bytes=256)
        blocks = list(bank.iter_blocks(observations, noise_variance=2.))
        self.assertEqual(bank.accounting()["gram_cache_bytes"], 256)
        self.assertEqual(bank.accounting()["gram_cache_evictions"], 2)
        # Retain the last block under a changed variance without recomputation.
        bank.reset_counters()
        changed = list(bank.iter_blocks(observations, noise_variance=4., start_block=2))
        self.assertEqual(bank.accounting()["gram_compute_calls"], 0)
        self.assertEqual(bank.accounting()["gram_cache_hits"], 1)
        torch.testing.assert_close(changed[0].gram, blocks[-1].gram/2, rtol=0, atol=0)

    def test_lru_and_immutable_basis(self):
        basis, observations = fixture()
        bank = ExhaustiveCorrelationBank(basis, 17., [0.], [0], 7, candidate_tile=1,
                                         template_cache_bytes=448, column_tile=1)
        reference = direct_fp64_reference(basis, observations, 17., [0.], [0], 7)
        basis[:] = 999  # Caller edits cannot invalidate already packed A.
        actual = list(bank.iter_blocks(observations))
        for block, (r, _) in zip(actual, reference):
            np.testing.assert_allclose(block.r, r, rtol=3e-15, atol=2e-14)
        self.assertLessEqual(bank.accounting()["template_cache_bytes"], 448)
        self.assertGreater(bank.accounting()["a_cache_evictions"], 0)
        for name, value in (("sample_rate", 1.), ("frequencies", np.array([.1])),
                            ("precision", "fp32"), ("block_samples", 5)):
            with self.assertRaisesRegex(AttributeError, "immutable"):
                setattr(bank, name, value)

    def test_fp32_and_backend_interface(self):
        basis, observations = fixture()
        reference = list(ExhaustiveCorrelationBank(basis, 17., [-.3, .2], [0, 5], 7).iter_blocks(observations))
        backend = RecordingBackend()
        for precision, extra in (("fp32", {}), ("native-fp4", {"backend": backend}),
                                 ("tensor", {"backend": backend})):
            bank = ExhaustiveCorrelationBank(basis, 17., [-.3, .2], [0, 5], 7,
                                             precision=precision, column_tile=2, **extra)
            for actual, expected in zip(bank.iter_blocks(observations), reference):
                torch.testing.assert_close(actual.r, expected.r, rtol=3e-6, atol=2e-6)
                torch.testing.assert_close(actual.gram, expected.gram, rtol=0, atol=0)
        self.assertGreater(backend.products, 0)
        self.assertGreater(backend.pack_calls, backend.products//10)

    def test_tile_indices_cover_exactly_once(self):
        basis, observations = fixture()
        bank = ExhaustiveCorrelationBank(basis, 11., [.2, -.1], [5, 0, 2], 7,
                                         frequency_tile=1, candidate_tile=3, column_tile=4)
        coverage = np.zeros((3, 3, 2, 4, 3), dtype=np.int64)
        for tile in bank.iter_tiles(observations):
            for trial, lag in zip(tile.trial_indices, tile.lag_indices):
                coverage[tile.block_index, trial, tile.frequency_slice, tile.candidate_slice, lag] += 1
        np.testing.assert_array_equal(coverage, 1)

    def test_rejects_truncation_invalid_grid_and_oversized_output(self):
        basis, observations = fixture()
        with self.assertRaisesRegex(ValueError, "support"):
            list(ExhaustiveCorrelationBank(basis, 11., [0.], [6], 7).iter_blocks(observations))
        for extra in ({"lags": [0, 0]}, {"lags": [-1]}, {"lags": [1.5]},
                      {"frequencies_hz": [np.inf]}, {"block_samples": 0},
                      {"precision": "native-fp4"}):
            args = dict(basis=basis, sample_rate=11., frequencies_hz=[0.], lags=[0], block_samples=7)
            args.update(extra)
            with self.assertRaises((TypeError, ValueError)):
                ExhaustiveCorrelationBank(**args)
        bank = ExhaustiveCorrelationBank(basis, 11., [0.], [0], 7)
        with self.assertRaisesRegex(ValueError, "output"):
            list(bank.iter_blocks(observations, max_output_bytes=1))
        for variance in (0., -1., np.inf):
            with self.assertRaises(ValueError):
                list(bank.iter_tiles(observations, variance))
        observations[0, 0] = np.nan
        with self.assertRaisesRegex(ValueError, "finite"):
            list(bank.iter_tiles(observations))
        basis[0, 0, 0] = 1e40
        with self.assertRaisesRegex(ValueError, "FP32"):
            ExhaustiveCorrelationBank(basis, 11., [0.], [0], 7, precision="fp32")

    def test_continuation_dt_and_phase_receiver(self):
        from triad_phase_frequency import PhaseFrequencyLikelihood, PhaseFrequencyModel
        basis, observations = fixture()
        frequencies, lags = [-.1, 0., .2], [0, 5]
        bank = ExhaustiveCorrelationBank(basis, 13.5, frequencies, lags, 7)
        blocks = list(bank.iter_blocks(observations, noise_variance=7.))
        continued = list(bank.iter_blocks(observations, noise_variance=7., start_block=2))
        self.assertEqual(continued[0].dt, blocks[2].dt)
        model = PhaseFrequencyModel(phase_states=16, frequency_grid_hz=(0.,),
                                    relative_phase_states=2, amplitude_nodes=(1.,), amplitude_weights=(1.,))
        count = len(observations)*len(frequencies)*len(lags)
        receiver = PhaseFrequencyLikelihood(count, bits=4, taps=2, model=model, dtype=torch.float64)
        for block in blocks:
            r = block.r.permute(0, 1, 3, 2, 4).reshape(count, 4, 2)
            receiver.update(r, block.gram, block.dt)
        self.assertEqual(receiver.scores().shape, (count, 4))
        self.assertTrue(bool(torch.isfinite(receiver.scores()).all()))

    def test_representation_decomposition(self):
        a = np.array([[1.1, -2.3, .04], [2.1, .02, 1.]])
        b = np.array([[3.2, 1.03, -.07]])
        qa, qb = np.round(a, 1), np.round(b, 1)
        products = representation_diagnostics(a, b, qa, qb)
        for name, expected in (("strict", a@b.T), ("template_only", qa@b.T),
                               ("observation_only", a@qb.T), ("both", qa@qb.T)):
            np.testing.assert_array_equal(products[name], expected)
        self.assertGreater(abs(products["strict"]-products["both"]).max(), .01)

    def test_native_complex_half_alignment(self):
        from triad_fp4_native import pack_rows_cpu, decode_rows
        rng = np.random.default_rng(417)
        template = torch.tensor(rng.normal(size=(2, 19))+1j*rng.normal(size=(2, 19)), dtype=torch.complex128)
        observed = torch.tensor(rng.normal(size=(3, 19))+1j*rng.normal(size=(3, 19)), dtype=torch.complex128)
        for format, group in (("nvfp4", 16), ("mxfp4", 32)):
            half = 32
            a = complex_template_rows(template, half_alignment=group).reshape(4, 2*half).float().contiguous()
            b = observation_rows(observed, half_alignment=group).float().contiguous()
            qa = decode_rows(pack_rows_cpu(a, format)).reshape(2, 2, 2*half)
            qb = decode_rows(pack_rows_cpu(b, format))
            torch.testing.assert_close(qa[:, 1, :half], -qa[:, 0, half:], rtol=0, atol=0)
            torch.testing.assert_close(qa[:, 1, half:], qa[:, 0, :half], rtol=0, atol=0)
            for value in (a, b, qa.reshape(4, 2*half), qb):
                self.assertTrue(bool((value[:, 19:half] == 0).all()))
                self.assertTrue(bool((value[:, half+19:] == 0).all()))
            represented = torch.complex(qa[:, 0, :half], qa[:, 0, half:])
            signal = torch.complex(qb[:, :half], qb[:, half:])
            product = representation_diagnostics(a, b, qa.reshape(4, 2*half), qb)["both"].reshape(2, 2, 3)
            np.testing.assert_allclose(product[:, 0]+1j*product[:, 1],
                                       represented.to(torch.complex128).numpy().conj()@signal.to(torch.complex128).numpy().T,
                                       rtol=0, atol=0)
            backend = RecordingBackend()
            backend.group_size = group
            bank = ExhaustiveCorrelationBank(template.numpy().reshape(2, 19, 1), 11., [0.], [0], 19,
                                             precision="native-fp4", backend=backend)
            blocks = list(bank.iter_blocks(observed))
            direct = direct_fp64_reference(template.numpy().reshape(2, 19, 1), observed, 11., [0.], [0], 19)
            np.testing.assert_allclose(blocks[0].r, direct[0][0], rtol=2e-6, atol=2e-6)
            self.assertGreater(bank.accounting()["a_complex_half_padding_bytes"], 0)
            self.assertGreater(bank.accounting()["b_complex_half_padding_bytes"], 0)

    def test_diagnostic_fp64_does_not_round_output_to_fp32(self):
        class DiagnosticBackend(RecordingBackend):
            def matmul(self, a, b):
                self.products += 1
                return a.double()@b.double().T
        basis, observed = fixture()
        # Isolate FP32 staging from product precision with exact staged inputs.
        basis, observed = basis.astype(np.complex64), observed.astype(np.complex64)
        backend = DiagnosticBackend()
        backend.group_size = 16
        bank = ExhaustiveCorrelationBank(basis, 11., [0.], [0, 5], 19,
                                         precision="diagnostic-fp64", backend=backend)
        block = next(bank.iter_blocks(observed))
        exact = direct_fp64_reference(basis, observed, 11., [0.], [0, 5], 19)[0][0]
        np.testing.assert_allclose(block.r.numpy(), exact, rtol=3e-15, atol=2e-14)
        rounded = block.r.to(torch.complex64).to(torch.complex128)
        self.assertGreater(float((block.r-rounded).abs().max()), 1e-8)
        self.assertEqual(bank.accounting()["admission_status"], "diagnostic_only_uncalibrated")
        with self.assertRaisesRegex(ValueError, "explicit backend"):
            ExhaustiveCorrelationBank(basis, 11., [0.], [0], 19, precision="diagnostic-fp64")
        # A diagnostic backend must not silently satisfy a native FP32 claim.
        native = ExhaustiveCorrelationBank(basis, 11., [0.], [0], 19,
                                           precision="native-fp4", backend=DiagnosticBackend())
        with self.assertRaisesRegex(ValueError, "dtype"):
            list(native.iter_blocks(observed))

    def test_geometry_cache_origin_and_byte_bound(self):
        basis, observed = fixture()
        for budget in (0, 400, 4096):
            bank = ExhaustiveCorrelationBank(basis, 13.5, [-.2, .3], [0, 2, 5], 7,
                                             column_tile=4, geometry_cache_bytes=budget)
            for origin in (0., 172800.125):
                actual = list(bank.iter_blocks(observed, time_origin_seconds=origin))
                exact = direct_fp64_reference(basis, observed, 13.5, [-.2, .3], [0, 2, 5], 7,
                                               time_origin_seconds=origin)
                for block, (r, _) in zip(actual, exact):
                    np.testing.assert_allclose(block.r, r, rtol=3e-14, atol=3e-14)
                self.assertLessEqual(bank.accounting()["geometry_cache_bytes"], budget)
            if budget == 0:
                self.assertEqual(bank.accounting()["geometry_cache_hits"], 0)

    def test_fused_reconstruction_cpu_algebra_and_guard(self):
        from triad_correlation_fusion import reconstruction_reference, reconstruct_correlations
        rng = np.random.default_rng(741)
        for dtype in (torch.float32, torch.float64):
            f, c, j, n = 3, 4, 2, 7
            product = torch.tensor(rng.normal(size=(2*f*c*j, n)), dtype=dtype)
            # Noncontiguous CFO slice and large exact FP64 phase coordinates.
            angles = torch.tensor(rng.normal(size=(2*f, n))*172800., dtype=torch.float64)
            phase = torch.exp(1j*angles)[::2]
            actual = reconstruction_reference(product, phase, 1/2.3, candidates=c, taps=j)
            expected = np.empty((n, f, c, j), dtype=np.complex128)
            for ni in range(n):
                for fi in range(f):
                    for ci in range(c):
                        for ji in range(j):
                            row = (fi*c+ci)*j+ji
                            expected[ni, fi, ci, ji] = complex(float(product[2*row, ni]), float(product[2*row+1, ni]))*complex(phase[fi, ni])*(1/2.3)
            np.testing.assert_allclose(actual, expected, rtol=3e-15, atol=3e-15)
            with self.assertRaisesRegex(RuntimeError, "requires CUDA"):
                reconstruct_correlations(product, phase, torch.tensor(1/2.3), candidates=c, taps=j)
        basis, _ = fixture()
        with self.assertRaisesRegex(ValueError, "requires CUDA"):
            ExhaustiveCorrelationBank(basis, 13.5, [0.], [0], 7, fused_reconstruction=True)

    def test_cuda_fused_reconstruction(self):
        if not CUDA_CHECKS:
            self.skipTest("explicit --cuda qualification was not requested")
        if not torch.cuda.is_available():
            self.fail("--cuda requested but CUDA is unavailable")
        from triad_correlation_fusion import reconstruction_reference, reconstruct_correlations
        torch.backends.cuda.matmul.allow_tf32 = False
        torch.set_float32_matmul_precision("highest")
        generator = torch.Generator().manual_seed(741)
        for f, c, j, n in ((3, 5, 2, 7), (65, 32, 1, 1056)):
            for dtype in (torch.float32, torch.float64):
                product = torch.randn((2*f*c*j, n), generator=generator, dtype=dtype).cuda()
                angles = torch.randn((2*f, n), generator=generator, dtype=torch.float64).cuda()*172800.
                phase = torch.exp(1j*angles)[::2]
                scale = torch.full((), 1/2.3, dtype=torch.float64, device="cuda")
                expected = reconstruction_reference(product, phase, scale, candidates=c, taps=j)
                actual = reconstruct_correlations(product, phase, scale, candidates=c, taps=j)
                torch.testing.assert_close(actual, expected, rtol=3e-14, atol=3e-14)
        basis, observed = fixture()
        for precision in ("fp32", "fp64"):
            arguments = dict(basis=basis, sample_rate=13.5, frequencies_hz=[-.213, 0., .217],
                             lags=[0, 2, 5], block_samples=7, device="cuda", precision=precision,
                             frequency_tile=2, candidate_tile=3, column_tile=4)
            a = list(ExhaustiveCorrelationBank(**arguments).iter_blocks(observed, 2.3, time_origin_seconds=172800.125))
            b = list(ExhaustiveCorrelationBank(**arguments, fused_reconstruction=True).iter_blocks(observed, 2.3, time_origin_seconds=172800.125))
            for expected, actual in zip(a, b):
                torch.testing.assert_close(actual.r, expected.r, rtol=3e-14, atol=3e-14)
                torch.testing.assert_close(actual.gram, expected.gram, rtol=0, atol=0)


if __name__ == "__main__":
    if CUDA_CHECKS:
        sys.argv.remove("--cuda")
    unittest.main()

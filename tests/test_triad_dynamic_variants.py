#!/usr/bin/env python3
"""Focused parity/projection controls; optional source-bound native replay."""
import copy
import math
import os
from pathlib import Path
import sys
import unittest

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_triad_dynamic_joint import fixture
from triad_dynamic_joint import DynamicJointReceiver, load_public
from triad_dynamic_variants import CoreProjectionReceiver, ScanJointReceiver, make_receiver


class VariantTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        torch.backends.cuda.matmul.allow_tf32 = False
        torch.set_float32_matmul_precision("highest")

    def _paired(self, device, precision):
        m, source = fixture(True)
        kwargs = dict(selected_epochs=(0,), arrival_seconds=(-.005, 0., .005),
            frequencies_hz=(-1., 0., 1.), block_seconds=.05,
            coherence_seconds=(.1, .5, 2., math.inf), auxiliary_half_width_hz=20.,
            device=device, precision=precision)
        serial = DynamicJointReceiver(m, source, [0], **kwargs)
        scan = ScanJointReceiver(m, source, [0], **kwargs)
        rng = np.random.default_rng(5901)
        signal = serial.source_basis(0, 800)[1, 4].sum(0)
        raw = signal[None, :]+rng.normal(size=(3, 800))+1j*rng.normal(size=(3, 800))
        correlations, _ = serial.correlations(raw)
        expected = serial.score_correlations(correlations)
        actual = scan.score_correlations(correlations)
        # This strong-pilot fixture has common H0/H1 evidence near8556.
        # FP32 serial and scan each differ from FP64 by about1e-3 after
        # subtraction; retain an explicit fixture envelope, not a universal
        # arithmetic bound. The independent near-gate checks remain0.01away.
        tolerance = 3e-10 if precision == "fp64" else 3e-3
        for key in ("bit_scores", "model_bit_scores", "hypothesis_log_evidence"):
            np.testing.assert_allclose(actual[key], expected[key], atol=tolerance, rtol=3e-6)
        np.testing.assert_array_equal(actual["bit_scores"].argmax(-1), expected["bit_scores"].argmax(-1))
        for margin in (-.01, .01):
            gate = expected["bit_scores"]+margin
            np.testing.assert_array_equal(actual["bit_scores"] >= gate, expected["bit_scores"] >= gate)
        if precision == "fp32":
            reference_kwargs = dict(kwargs, device="cpu", precision="fp64")
            reference = DynamicJointReceiver(m, source, [0], **reference_kwargs).score(raw)
            np.testing.assert_allclose(actual["bit_scores"], reference["bit_scores"], atol=3e-3, rtol=3e-6)
            for margin in (-.01, .01):
                gate = reference["bit_scores"]+margin
                np.testing.assert_array_equal(actual["bit_scores"] >= gate, reference["bit_scores"] >= gate)
        self.assertNotEqual(serial.procedure_id, scan.procedure_id)
        self.assertEqual(len(scan.variant_policy["source_sha256"]), 5)
        self.assertEqual(len(scan.variant_policy["resolved_projection_sha256"]), 64)
        self.assertTrue(all(x["execution"] == "affine-parallel-scan" for x in actual["scan_backend"]))

    def test_cpu_scan_matches_frozen_mixture_and_near_gate_decisions(self):
        for precision in ("fp32", "fp64"):
            with self.subTest(precision=precision):
                self._paired("cpu", precision)

    @unittest.skipUnless(torch.cuda.is_available(), "CUDA hardware unavailable; no GPU pass claimed")
    def test_cuda_scan_matches_frozen_mixture_and_near_gate_decisions(self):
        self._paired("cuda", "fp32")

    def test_core_projection_preserves_physics_and_declares_loss(self):
        m, source = fixture(True)
        m["channel"].update(channel_evolution="fixed-time-gaussian-v1",
            random_doppler_width_hz=2., selective_coefficient_law="gaussian-erf-v1",
            selective_modulation_rate_hz=10., selective_slow_depth=.65, selective_fast_depth=.65)
        before = copy.deepcopy(m)
        kwargs = dict(selected_epochs=(0,), block_seconds=.1, coherence_seconds=(.2,),
                      auxiliary_half_width_hz=20., precision="fp64")
        with self.assertRaisesRegex(ValueError, "overlaps excluded"):
            DynamicJointReceiver(m, source, [0], **kwargs)
        core = CoreProjectionReceiver(m, source, [0], **kwargs)
        self.assertEqual(m, before)
        self.assertEqual(core.manifest["channel"]["selective_fast_depth"], .65)
        self.assertEqual(core.required_bands_hz[0], (-69., 69.))  #60+3Hzcenter+3*2Hzwidth
        self.assertIn("lossy", core.variant_policy["limitation"])
        # The metadata change affects only selection, never the physical source
        # alternatives entering the retained bins.
        quiet = copy.deepcopy(m)
        quiet["channel"]["selective_slow_depth"] = 0.
        quiet["channel"]["selective_fast_depth"] = 0.
        full = ScanJointReceiver(quiet, source, [0], **kwargs)
        np.testing.assert_array_equal(core.source_basis(0, 800), full.source_basis(0, 800))
        self.assertNotEqual(core.procedure_id, full.procedure_id)

    def test_explicit_scan_guard_fallback_and_factory(self):
        m, source = fixture(False)
        kwargs = dict(selected_epochs=(0,), block_seconds=.1, coherence_seconds=(.2,), precision="fp64")
        serial = make_receiver(m, source, [0], execution="serial", **kwargs)
        scan = make_receiver(m, source, [0], scan_residual_limit=1e-30, **kwargs)
        raw = serial.source_basis(0, 800)[1, 0].sum(0)
        r, _ = serial.correlations(raw)
        a, b = serial.score_correlations(r), scan.score_correlations(r)
        np.testing.assert_array_equal(a["bit_scores"], b["bit_scores"])
        self.assertEqual(b["scan_backend"][0]["execution"], "serial-fallback")
        with self.assertRaisesRegex(ValueError, "core variant"):
            make_receiver(m, source, [0], execution="serial", projection="core", **kwargs)
        with self.assertRaisesRegex(ValueError, "caller-supplied"):
            CoreProjectionReceiver(m, source, [0], required_bands_hz=((-50, 50),), **kwargs)

    @unittest.skipUnless(os.environ.get("TRIAD_DYNAMIC_NATIVE_CAPTURE"), "optional native replay not requested")
    def test_native_capture_four_coherences_serial_scan(self):
        directory = Path(os.environ["TRIAD_DYNAMIC_NATIVE_CAPTURE"])
        m, source, epochs, raw, valid = load_public(directory)
        kwargs = dict(valid=valid, selected_epochs=(0,), block_seconds=.25,
            coherence_seconds=(.1, .5, 2., math.inf), precision="fp32")
        serial = DynamicJointReceiver(m, source, epochs, **kwargs)
        scan = ScanJointReceiver(m, source, epochs, **kwargs)
        r, _ = serial.correlations(raw)
        expected, actual = serial.score_correlations(r), scan.score_correlations(r)
        np.testing.assert_allclose(actual["hypothesis_log_evidence"],
            expected["hypothesis_log_evidence"], atol=2e-3, rtol=2e-5)
        np.testing.assert_allclose(actual["bit_scores"], expected["bit_scores"], atol=2e-3, rtol=2e-5)
        np.testing.assert_array_equal(actual["bit_scores"].argmax(-1), expected["bit_scores"].argmax(-1))
        for gap in (-.01, .01):
            gate = expected["bit_scores"]+gap
            np.testing.assert_array_equal(actual["bit_scores"] >= gate, expected["bit_scores"] >= gate)


if __name__ == "__main__":
    unittest.main()

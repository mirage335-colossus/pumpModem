#!/usr/bin/env python3
"""Cheap numerical/physical-boundary checks for the raw joint adapter."""
import copy
import math
from pathlib import Path
import sys
import unittest

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from triad_dynamic_joint import (BlockProjection, DynamicJointReceiver,
    ProjectedJointReceiver, public_spectral_bands, _sample_cubic, _sample_cubic_rows)


def dense_evidence(basis, observation, variance, powers, coherence, block_samples, fs):
    """Independent dense Gaussian covariance, including inter-block states."""
    h, candidates, taps, n = basis.shape
    blocks = (n+block_samples-1)//block_samples
    centers = (np.arange(blocks)+.5)*block_samples/fs
    temporal = np.exp(-abs(centers[:, None]-centers[None, :])/coherence)
    cov = np.kron(temporal, np.diag(powers))
    result = np.empty((h, candidates))
    for hi in range(h):
        for ci in range(candidates):
            design = np.zeros((n, blocks*taps), np.complex128)
            for b in range(blocks):
                lo, end = b*block_samples, (b+1)*block_samples
                design[lo:end, b*taps:(b+1)*taps] = basis[hi, ci, :, lo:end].T
            sigma = variance*np.eye(n)+design@cov@design.conj().T
            sign, logdet = np.linalg.slogdet(sigma/variance)
            result[hi, ci] = (np.vdot(observation, observation)/variance-
                np.vdot(observation, np.linalg.solve(sigma, observation))).real-logdet
            assert abs(sign-1) < 1e-10
    return result


def fixture(helper=False):
    fs, rows = 2000, 800
    time = np.arange(200)/fs
    templates = np.zeros((1, 2, 200), np.complex128)
    for bit in (0, 1):
        for carrier, freq in enumerate((-30., 0., 30.)):
            envelope = 1+.2*np.cos(2*np.pi*(11+bit*7+carrier*3)*time)
            templates[0, bit] += envelope*np.exp(2j*np.pi*freq*time+1j*bit*carrier)
    templates /= np.sqrt(3)
    m = dict(sample_rate_hz=fs, helper_enabled=helper, public_epoch_seconds=1000,
        format=dict(occupied_bandwidth_hz=120.),
        channel=dict(spread_delay_ms=2., spread_doppler_hz=3.,
            nominal_conversion_if_hz=600., selective_modulation_rate_hz=0.,
            selective_slow_depth=0., selective_fast_depth=0.),
        calendar=dict(capture_samples=rows, capture_lead_seconds=.02,
            data_sample_offset=240, arrival_prior_seconds=[-.01, .01],
            helper_start_seconds=0., helper_end_seconds=.35),
        helper_plan=dict(helper_gain=4., high_hz=950., low_hz=200.,
            auxiliary_hz=400., pilot_dwell_gating=False),
        power=dict(post_plan_composite_gain=1., complex_noise_variance=2.))
    return m, templates


def scalar_native_basis(manifest, source, epochs, candidates, pilot_use, start, stop):
    """Frozen pre-reuse source construction, independent of grouped scheduling."""
    m = manifest
    fs, ch, cal, hp = m["sample_rate_hz"], m["channel"], m["calendar"], m["helper_plan"]
    t = np.arange(start, stop, dtype=np.float64)/fs-cal["capture_lead_seconds"]
    delay, doppler = ch["spread_delay_ms"]*.001, ch["spread_doppler_hz"]
    rays = ((0., 0.), (delay, doppler)) if delay or doppler else ((0., 0.),)
    result = np.zeros((3, len(candidates), len(rays), stop-start), np.complex128)
    def smooth(x):
        x = np.clip(x, 0., 1.)
        return x*x*x*(10+x*(-15+6*x))
    fractions = {"high": 1/(1+.501187233627272285+.125892541179416721),
                 "low": .501187233627272285/(1+.501187233627272285+.125892541179416721)}
    gain = hp["helper_gain"]*m["power"]["post_plan_composite_gain"]
    for ci, (epoch, arrival, frequency, clock) in enumerate(candidates):
        rotation = np.exp(2j*np.pi*np.remainder(frequency*t, 1.))
        for ri, (ray_delay, ray_doppler) in enumerate(rays):
            ts = (t-arrival-ray_delay)*(1+clock*1e-6)
            pilot = np.zeros(stop-start, np.complex128)
            if m.get("helper_enabled", True) and pilot_use != "none":
                envelope = smooth((ts-cal["helper_start_seconds"])/.004)*smooth(
                    (cal["helper_end_seconds"]-ts)/.004)
                if hp.get("pilot_dwell_gating", False):
                    local = np.remainder((m["public_epoch_seconds"]+epoch)%2+ts, .4)
                    envelope *= smooth(np.minimum(local, .4-local)/.004)
                for name in ("low", "high"):
                    if pilot_use in ("both", name):
                        tone = hp[name+"_hz"]-ch["nominal_conversion_if_hz"]
                        pilot += gain*math.sqrt(fractions[name])*envelope*np.exp(
                            2j*np.pi*np.remainder(tone*ts, 1.))
            common = rotation*np.exp(2j*np.pi*np.remainder(ray_doppler*t, 1.))
            result[0, ci, ri] = pilot*common
            for bit in (0, 1):
                data = _sample_cubic(source[epochs.index(epoch), bit],
                                     (ts-cal["data_sample_offset"]/fs)*fs)
                result[bit+1, ci, ri] = (data+pilot)*common
    return result


class JointTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        torch.backends.cuda.matmul.allow_tf32 = False
        torch.set_float32_matmul_precision("highest")

    def test_projection_is_orthonormal_and_excludes_integer_auxiliary(self):
        p = BlockProjection.make(64, 64., ((-5., 5.), (15., 17.)), ((8., 12.),))
        matrix = p.numpy(np.eye(64))
        np.testing.assert_allclose(matrix.conj().T@matrix, np.eye(len(p.indices)), atol=2e-15)
        tone = np.exp(2j*np.pi*10*np.arange(64)/64)
        self.assertLess(np.linalg.norm(p.numpy(tone)), 3e-14)
        with self.assertRaisesRegex(ValueError, "overlaps excluded"):
            BlockProjection.make(64, 64., ((-5., 10.),), ((9., 12.),))
        with self.assertRaisesRegex(ValueError, "block too short|bin width"):
            BlockProjection.make(8, 64., ((0., 1.),), ((2., 3.),))

    def test_shared_cubic_rows_preserve_exact_endpoint_stencils(self):
        rng = np.random.default_rng(813)
        source = rng.normal(size=(2, 9))+1j*rng.normal(size=(2, 9))
        position = np.array([-3., -2., -1.9999, -1., -.25, 0., .25, 7.9,
                             8., 8.9, 9., 9.9999, 10., 11.])
        expected = np.stack([_sample_cubic(row, position) for row in source])
        np.testing.assert_array_equal(_sample_cubic_rows(source, position), expected)
        self.assertEqual(_sample_cubic_rows(source, position[:0]).shape, (2, 0))

    def test_grouped_source_matches_scalar_reference_with_pilots_and_clock(self):
        m, one_epoch = fixture(True)
        m["helper_plan"]["pilot_dwell_gating"] = True
        source = np.concatenate((one_epoch, one_epoch[:, :, ::-1]), axis=0)
        epochs = (0, 1)
        # Duplicate source geometry with distinct CFO, fractional boundaries,
        # a second epoch, both clock signs, and the complete delayed stencil.
        coordinates = ((0, -.00913, -7.25, -250.), (0, -.00913, 2.75, -250.),
                       (1, .00871, 2.75, 180.), (0, .00017, -7.25, 180.))
        for pilot in ("both", "low", "high", "none"):
            for precision in ("fp32", "fp64"):
                with self.subTest(pilot=pilot, precision=precision):
                    receiver = DynamicJointReceiver(m, source, epochs,
                        candidate_coordinates=coordinates, block_seconds=.05,
                        coherence_seconds=(.2, math.inf), pilot_use=pilot,
                        # This is a source-construction oracle, including
                        # gated-pilot skirts. Public exclusion feasibility is
                        # independently exercised by the guard tests below.
                        required_bands_hz=((-999., 999.),),
                        auxiliary_exclusion_hz=(), precision=precision)
                    oracle = lambda lo, hi: scalar_native_basis(
                        m, source, epochs, coordinates, pilot, lo, hi)
                    expected = oracle(0, 800)
                    np.testing.assert_array_equal(receiver.source_basis(0, 800), expected)
                    for start, stop in ((0, 150), (150, 251), (251, 504), (504, 800)):
                        np.testing.assert_array_equal(receiver.source_basis(start, stop),
                                                      expected[..., start:stop])
                    reference = ProjectedJointReceiver(sample_rate=2000, samples=800,
                        block_samples=100, basis=oracle,
                        required_bands=receiver.required_bands_hz,
                        excluded_bands=receiver.auxiliary_exclusion_hz,
                        noise_variance=2., stationary_power=(.5, .5),
                        coherence_seconds=(.2, math.inf), precision=precision,
                        candidate_count=len(coordinates))
                    raw = np.stack([expected[bit+1, 0].sum(0) for bit in (0, 1)])
                    np.testing.assert_array_equal(receiver.score(raw)["bit_scores"],
                                                  reference.score(raw)["bit_scores"])

    def test_zero_basis_blocks_keep_times_and_covariance_transitions(self):
        rng = np.random.default_rng(814)
        basis = np.zeros((3, 1, 1, 48), np.complex128)
        basis[1:, :, :, 12:24] = rng.normal(size=(2, 1, 1, 12))*.2
        basis[1:, :, :, 36:48] = rng.normal(size=(2, 1, 1, 12))*.2
        raw = rng.normal(size=48)+1j*rng.normal(size=48)
        receiver = ProjectedJointReceiver(sample_rate=48, samples=48, block_samples=12,
            basis=lambda lo, hi: basis[..., lo:hi], required_bands=((-23.99, 23.99),),
            stationary_power=(1.,), coherence_seconds=(.15,), precision="fp64")
        self.assertEqual(receiver.zero_basis_blocks, {0, 2})
        self.assertEqual(len(receiver.blocks), 4)
        centers = [(start+stop-1)/(2*48) for start, stop in
                   ((0, 12), (12, 24), (24, 36), (36, 48))]
        np.testing.assert_array_equal(receiver.elapsed, (0., *np.diff(centers)))
        scores = receiver.score(raw)["hypothesis_log_evidence"][0, 0]
        expected = dense_evidence(basis, raw, 1., (1.,), .15, 12, 48)[:, 0]
        np.testing.assert_allclose(scores, expected, rtol=2e-10, atol=2e-10)

    def test_raw_joint_matches_independent_dense_covariance(self):
        rng = np.random.default_rng(810)
        basis = (rng.normal(size=(3, 2, 2, 24))+1j*rng.normal(size=(3, 2, 2, 24)))*.2
        basis[0] *= .1
        raw = rng.normal(size=24)+1j*rng.normal(size=24)
        receiver = ProjectedJointReceiver(sample_rate=24., samples=24, block_samples=8,
            basis=lambda lo, hi: basis[..., lo:hi], required_bands=((-11.99, 11.99),),
            noise_variance=2., stationary_power=(.4, .6), coherence_seconds=(.7, math.inf),
            precision="fp64", candidate_count=2)
        result = receiver.score(raw)
        for model, coherence in enumerate((.7, math.inf)):
            expected = dense_evidence(basis, raw, 2., (.4, .6), coherence, 8, 24.)
            mixed = np.logaddexp.reduce(expected, axis=-1)-math.log(2)
            np.testing.assert_allclose(result["hypothesis_log_evidence"][model, 0], mixed,
                                       rtol=2e-10, atol=2e-10)
        r, _ = receiver.correlations(raw)
        repeated = receiver.score_correlations(r)
        np.testing.assert_array_equal(result["bit_scores"], repeated["bit_scores"])

    def test_no_helper_zero_null_and_fp32_reference(self):
        m, source = fixture(False)
        kwargs = dict(selected_epochs=(0,), block_seconds=.1, coherence_seconds=(.2, math.inf),
                      pilot_use="both", auxiliary_half_width_hz=20.)
        receiver = DynamicJointReceiver(m, source, [0], precision="fp64", **kwargs)
        raw = receiver.source_basis(0, 800)[2, 0].sum(axis=0)
        result = receiver.score(np.stack([raw, raw*.2]))
        np.testing.assert_allclose(result["hypothesis_log_evidence"][:, :, 0], 0., atol=1e-15)
        self.assertEqual(receiver.auxiliary_exclusion_hz, ())
        fp32 = DynamicJointReceiver(m, source, [0], precision="fp32", **kwargs)
        np.testing.assert_allclose(fp32.score(raw)["bit_scores"], result["bit_scores"][:1],
                                   atol=3e-4, rtol=3e-5)
        self.assertGreater(result["bit_scores"][0, 1], result["bit_scores"][0, 0])

    def test_both_single_and_no_pilot_public_models(self):
        m, source = fixture(True)
        receivers = [DynamicJointReceiver(m, source, [0], selected_epochs=(0,),
            block_seconds=.1, coherence_seconds=(.2,), pilot_use=mode,
            auxiliary_half_width_hz=20., precision="fp64") for mode in ("both", "low", "high", "none")]
        bases = [r.source_basis(0, 800) for r in receivers]
        np.testing.assert_allclose(bases[0][0], bases[1][0]+bases[2][0], atol=1e-13)
        np.testing.assert_array_equal(bases[3][0], 0.)
        self.assertEqual(len({r.procedure_id for r in receivers}), 4)
        # A declared weaker pilot reduces its contribution; the other tone
        # remains present in the same common-channel hypothesis.
        weak = copy.deepcopy(m); weak["helper_plan"]["helper_gain"] *= .1
        rweak = DynamicJointReceiver(weak, source, [0], selected_epochs=(0,),
            block_seconds=.1, coherence_seconds=(.2,), auxiliary_half_width_hz=20., precision="fp64")
        np.testing.assert_allclose(rweak.source_basis(0, 800)[0], .1*bases[0][0], atol=1e-13)

    def test_erasure_retains_other_raw_rows_and_advances_physical_time(self):
        m, source = fixture(False)
        valid = np.ones(800, np.uint8); valid[211] = 0
        r = DynamicJointReceiver(m, source, [0], selected_epochs=(0,), valid=valid,
            block_seconds=.1, coherence_seconds=(.2,), precision="fp64")
        self.assertEqual(r.erased_samples, 1)
        np.testing.assert_allclose(r.elapsed, [0., .1, .1, .1])
        correlations, _ = r.correlations(np.ones(800, np.complex128))
        self.assertGreater(float(correlations[1].abs().max()), 0.)
        self.assertGreater(float(r.grams[1].abs().max()), 0.)
        self.assertEqual(len(r.blocks[1][2].indices), 199)
        expected = r.source_basis(200, 400)
        expected = np.delete(expected, 11, axis=-1)
        gram = np.einsum("hctn,hcsn->hcts", expected.conj(), expected)/2.
        np.testing.assert_allclose(r.grams[1].numpy(), gram, atol=1e-12)
        m["helper_enabled"] = True
        with self.assertRaisesRegex(ValueError, "partial erasures"):
            DynamicJointReceiver(m, source, [0], selected_epochs=(0,), valid=valid,
                block_seconds=.1, coherence_seconds=(.2,), auxiliary_half_width_hz=20.)

    def test_public_guard_accounts_for_random_support_and_absence(self):
        m, _ = fixture(True)
        m["channel"].update(channel_evolution="fixed-time-gaussian-v1", random_doppler_width_hz=2.,
                            selective_fast_depth=.65, selective_modulation_rate_hz=10.,
                            selective_coefficient_law="gaussian-erf-v1")
        required, excluded = public_spectral_bands(m, auxiliary_half_width_hz=20.)
        with self.assertRaisesRegex(ValueError, "overlaps excluded"):
            BlockProjection.make(200, 2000., required, excluded)
        m["helper_enabled"] = False
        required, excluded = public_spectral_bands(m, auxiliary_half_width_hz=20.)
        BlockProjection.make(200, 2000., required, excluded)
        self.assertEqual(excluded, ())

    def test_short_remainder_is_merged_not_padded_or_lost(self):
        basis = np.ones((3, 1, 1, 25), np.complex128); basis[0] = 0
        r = ProjectedJointReceiver(sample_rate=10, samples=25, block_samples=10,
            basis=lambda lo, hi: basis[..., lo:hi], required_bands=((-1., 1.),),
            stationary_power=(1.,), coherence_seconds=(1.,))
        self.assertEqual([(b[0], b[1]) for b in r.blocks], [(0, 10), (10, 25)])
        self.assertEqual(sum(b[1]-b[0] for b in r.blocks), 25)

    def test_invalid_shape_resource_and_arrival_rejected(self):
        m, source = fixture(False)
        with self.assertRaisesRegex(ValueError, "arrival bank"):
            DynamicJointReceiver(m, source, [0], arrival_seconds=(1.,))
        with self.assertRaises(MemoryError):
            DynamicJointReceiver(m, source, [0], selected_epochs=(0,), memory_limit_bytes=1)

    def test_preflight_rejects_before_source_callback_and_batch_allocation(self):
        called = []
        def forbidden(lo, hi):
            called.append((lo, hi))
            raise AssertionError("basis must not run beyond memory budget")
        with self.assertRaises(MemoryError):
            ProjectedJointReceiver(sample_rate=100, samples=1000, block_samples=100,
                basis=forbidden, required_bands=((-40., 40.),), candidate_count=1000,
                memory_limit_bytes=1_000_000)
        self.assertEqual(called, [])
        basis = np.ones((3, 1, 1, 20), np.complex128)
        r = ProjectedJointReceiver(sample_rate=100, samples=20, block_samples=10,
            basis=lambda lo, hi: basis[..., lo:hi], required_bands=((-40., 40.),),
            stationary_power=(1.,), coherence_seconds=(1.,), memory_limit_bytes=10000)
        with self.assertRaisesRegex(MemoryError, "batch"):
            r.correlations(np.ones((50, 20), np.complex128))


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Small exhaustive comparisons for tiled joint acquisition; no sensitivity campaign."""
from __future__ import annotations

import copy
import itertools
import math
from pathlib import Path
import sys
import unittest

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_triad_dynamic_joint import fixture as base_fixture
from triad_dynamic_acquire import TiledAcquisition, projection_digest, pilot_timing_guard
from triad_dynamic_joint import DynamicJointReceiver, public_spectral_bands
from triad_dynamic_variants import ScanJointReceiver


def fixture(helper=False):
    # These deliberately controlled numerical fixtures have no sample-clock
    # uncertainty or moving delay. Do not infer that from absent metadata in
    # real capture manifests.
    manifest, source = base_fixture(helper)
    manifest['channel'].update(sample_clock_pair_ppm=0., delay_excursion_seconds=0.)
    return manifest, source


class TiledAcquisitionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        torch.set_float32_matmul_precision('highest')
        torch.backends.cuda.matmul.allow_tf32 = False

    def test_partition_has_no_duplicate_or_missing_candidates(self):
        m, source = fixture(False)
        source = np.repeat(source, 2, axis=0)
        epochs, arrivals, frequencies, clocks = (-1, 0), (-.01, -.005, 0., .005, .01), (-2., 3.), (-.01, .01)
        tiled = TiledAcquisition(m, source, epochs, arrival_seconds=arrivals,
            frequencies_hz=frequencies, clock_ppm=clocks, timing_tile_size=2)
        coordinates = [coordinate for tile in tiled.tiles() for coordinate in tile.coordinates()]
        expected = set(itertools.product(epochs, arrivals, frequencies, clocks))
        self.assertEqual(set(coordinates), expected)
        self.assertEqual(len(coordinates), len(expected))
        self.assertEqual(tiled.candidates, 40)
        self.assertEqual(tiled.tile_count, 24)
        self.assertEqual({tile.candidates for tile in tiled.tiles()}, {1, 2})

    def test_helper_exhaustive_matches_irregular_tiles_fp32_and_fp64(self):
        m, source = fixture(True)
        source = np.concatenate((source, source*np.exp(.3j)), axis=0)
        epochs, arrivals, frequencies = (-1, 0), (-.01, -.005, 0., .005, .01), (-2., 3.)
        kwargs = dict(arrival_seconds=arrivals, frequencies_hz=frequencies,
            clock_ppm=(0.,), block_seconds=.1, coherence_seconds=(.2, math.inf),
            pilot_use='both', auxiliary_half_width_hz=20., memory_limit_bytes=64*1024**2)
        required, excluded = public_spectral_bands(m, pilot_use='both', cfo_bound_hz=3.,
                                                   auxiliary_half_width_hz=20.)
        rng = np.random.default_rng(7803)
        reference = ScanJointReceiver(m, source, epochs, precision='fp64',
            required_bands_hz=required, auxiliary_exclusion_hz=excluded, **kwargs)
        signal = reference.source_basis(0, 800)[2, 7].sum(0)
        raw = signal[None, :]+.2*(rng.normal(size=(3, 800))+1j*rng.normal(size=(3, 800)))
        for precision in ('fp64', 'fp32'):
            with self.subTest(precision=precision):
                exhaustive = ScanJointReceiver(m, source, epochs, precision=precision,
                    required_bands_hz=required, auxiliary_exclusion_hz=excluded, **kwargs)
                expected = exhaustive.score(raw)
                tiled = TiledAcquisition(m, source, epochs, precision=precision,
                                         timing_tile_size=2, arrival_model='discrete', **kwargs)
                actual = tiled.score(raw, trial_batch_size=2)
                tolerance = 5e-10 if precision == 'fp64' else 3e-3
                for name in ('hypothesis_log_evidence', 'model_bit_scores', 'bit_scores'):
                    np.testing.assert_allclose(actual[name], expected[name], atol=tolerance, rtol=3e-6)
                np.testing.assert_array_equal(actual['bit_scores'].argmax(-1), expected['bit_scores'].argmax(-1))
                self.assertEqual(actual['work']['candidates'], 20)
                self.assertEqual(actual['work']['total_candidate_trial_evaluations'], 60)
                self.assertEqual(len(actual['tiles']), 12)
                self.assertEqual({t['projection_sha256'] for t in actual['tiles']}, {projection_digest(exhaustive)})
                self.assertEqual(tiled.required_bands_hz, required)
                self.assertEqual(tiled.excluded_bands_hz, excluded)
                self.assertLessEqual(actual['work']['total_preparation_peak_bytes_estimate'], 64*1024**2)
        # H0 contains Helper, and is timing dependent. The tested weighted
        # mixture cannot be replaced by a single fixed H0 normalizer.
        endpoint_scores = []
        for arrival in (-.01, .01):
            one = ScanJointReceiver(m, source, epochs, selected_epochs=(0,), arrival_seconds=(arrival,),
                frequencies_hz=(3.,), block_seconds=.1, coherence_seconds=(.2, math.inf),
                required_bands_hz=required, auxiliary_exclusion_hz=excluded,
                auxiliary_half_width_hz=20., precision='fp64')
            endpoint_scores.append(one.score(raw)['hypothesis_log_evidence'][:, :, 0])
        self.assertGreater(float(np.max(np.abs(endpoint_scores[0]-endpoint_scores[1]))), 1e-4)

    def test_no_helper_serial_and_partial_erasure_match_exhaustive(self):
        m, source = fixture(False)
        kwargs = dict(arrival_seconds=(-.006, 0., .006), frequencies_hz=(0.,),
            coherence_seconds=(.2, math.inf), block_seconds=.1, precision='fp64',
            memory_limit_bytes=64*1024**2)
        valid = np.ones(800, np.uint8); valid[215] = 0; valid[400:600] = 0
        raw = np.exp(.03j*np.arange(800))[None, :]
        full = DynamicJointReceiver(m, source, (0,), valid=valid, **kwargs).score(raw)
        tiled = TiledAcquisition(m, source, (0,), timing_tile_size=2, execution='serial', **kwargs)
        actual = tiled.score(raw, valid)
        np.testing.assert_allclose(actual['hypothesis_log_evidence'], full['hypothesis_log_evidence'], atol=3e-11)
        # Weighted logaddexp changes the reduction order; log(2+1)-log(3)
        # can leave one FP64 ulp although every individual null evidence is0.
        np.testing.assert_allclose(actual['hypothesis_log_evidence'][:, :, 0], 0.,
                                   rtol=0., atol=2*np.finfo(np.float64).eps)

    def test_input_budget_and_public_bank_guards(self):
        m, source = fixture(False)
        tiled = TiledAcquisition(m, source, (0,), memory_limit_bytes=source.nbytes+100)
        with self.assertRaisesRegex(MemoryError, 'leave no memory'):
            tiled.score(np.ones((2, 800), np.complex128))
        with self.assertRaisesRegex(ValueError, 'unique'):
            TiledAcquisition(m, source, (0,), arrival_seconds=(0., 0.))
        with self.assertRaisesRegex(ValueError, 'arrival prior'):
            TiledAcquisition(m, source, (0,), arrival_seconds=(.02,))
        with self.assertRaisesRegex(ValueError, 'candidate cap'):
            TiledAcquisition(m, source, (0,), arrival_seconds=(-.01, 0., .01), maximum_candidates=2)
        with self.assertRaisesRegex(ValueError, 'missing'):
            TiledAcquisition(m, source, (0,), selected_epochs=(1,))

    def test_core_selector_uses_full_frequency_bank_without_mutating_channel(self):
        m, source = fixture(True)
        m['channel'].update(selective_slow_depth=.65, selective_fast_depth=.65,
            selective_modulation_rate_hz=10., selective_coefficient_law='gaussian-erf-v1',
            random_doppler_width_hz=2.)
        before = copy.deepcopy(m)
        tiled = TiledAcquisition(m, source, (0,), projection='core', frequencies_hz=(-2., 4.),
                                 auxiliary_half_width_hz=20., arrival_model='discrete')
        self.assertEqual(tiled.required_bands_hz[0], (-73., 73.))
        self.assertEqual(m, before)
        self.assertIn('lossy', tiled.policy['projection_scope'])

    def test_continuous_timing_guard_selects_none_before_projection(self):
        m, source = fixture(True)
        kwargs = dict(arrival_seconds=(-.01, 0., .01), block_seconds=.1,
            coherence_seconds=(.2, math.inf), auxiliary_half_width_hz=20., precision='fp64')
        guarded = TiledAcquisition(m, source, (0,), pilot_use='both', **kwargs)
        explicit = TiledAcquisition(m, source, (0,), pilot_use='none', **kwargs)
        self.assertEqual(guarded.requested_pilot_use, 'both')
        self.assertEqual(guarded.pilot_use, 'none')
        self.assertEqual(guarded.timing_guard['reason'], 'helper_phase_timing_unresolved')
        self.assertEqual(guarded.required_bands_hz, explicit.required_bands_hz)
        self.assertEqual(guarded.excluded_bands_hz, explicit.excluded_bands_hz)
        self.assertNotEqual(guarded.procedure_id, explicit.procedure_id)
        rng = np.random.default_rng(371)
        raw = rng.normal(size=(2, 800))+1j*rng.normal(size=(2, 800))
        actual, expected = guarded.score(raw), explicit.score(raw)
        np.testing.assert_array_equal(actual['hypothesis_log_evidence'], expected['hypothesis_log_evidence'])
        self.assertEqual(actual['work']['effective_pilot_use'], 'none')
        self.assertEqual(actual['work']['pilot_timing_guard'], guarded.policy['pilot_timing_guard'])

    def test_fine_continuous_timing_keeps_requested_pilots(self):
        m, source = fixture(True)
        bank = tuple(np.linspace(-.01, .01, 201))
        guard = pilot_timing_guard(m, bank)
        self.assertEqual(guard['reason'], 'pilot_timing_resolved')
        self.assertEqual(guard['effective_pilot_use'], 'both')
        self.assertAlmostEqual(guard['pilot_data_span_hz'], 750.)
        expected = math.acos(math.sqrt(10**(-.5/10)))/(math.pi*750)
        self.assertAlmostEqual(guard['allowed_cover_radius_seconds'], expected)
        tiled = TiledAcquisition(m, source, (0,), arrival_seconds=bank, auxiliary_half_width_hz=20.)
        full = public_spectral_bands(m, auxiliary_half_width_hz=20.)
        self.assertEqual((tiled.required_bands_hz, tiled.excluded_bands_hz), full)
        self.assertEqual(tiled.policy['pilot_use'], 'both')

    def test_guard_covers_nonuniform_gaps_and_uncovered_endpoints(self):
        m, _ = fixture(True)
        nonuniform = pilot_timing_guard(m, (-.01, -.009, .009, .01))
        self.assertAlmostEqual(nonuniform['continuous_cover_radius_seconds'], .009)
        self.assertAlmostEqual(nonuniform['maximum_grid_gap_seconds'], .018)
        endpoints = pilot_timing_guard(m, (-.005, -.004))
        self.assertAlmostEqual(endpoints['uncovered_left_seconds'], .005)
        self.assertAlmostEqual(endpoints['uncovered_right_seconds'], .014)
        self.assertAlmostEqual(endpoints['continuous_cover_radius_seconds'], .014)
        single = pilot_timing_guard(m, (0.,))
        self.assertEqual(single['continuous_cover_radius_seconds'], .01)
        self.assertEqual(single['reason'], 'helper_phase_timing_unresolved')

    def test_guard_single_pilot_spans_data_and_discrete_is_explicit(self):
        m, _ = fixture(True)
        low = pilot_timing_guard(m, (0.,), pilot_use='low')
        high = pilot_timing_guard(m, (0.,), pilot_use='high')
        self.assertEqual(low['pilot_data_span_hz'], 460.)
        self.assertEqual(high['pilot_data_span_hz'], 410.)
        self.assertEqual(low['effective_pilot_use'], 'none')
        discrete = pilot_timing_guard(m, (0.,), arrival_model='discrete')
        self.assertFalse(discrete['guard_applied'])
        self.assertEqual(discrete['effective_pilot_use'], 'both')
        self.assertEqual(discrete['reason'], 'explicit_discrete_arrival_prior')
        clocked = pilot_timing_guard(m, (0.,), clock_ppm=(-100., 100.))
        self.assertAlmostEqual(clocked['pilot_data_span_hz'], 750*1.0001)
        with self.assertRaises(ValueError):
            pilot_timing_guard(m, (0.,), arrival_model='unknown')
        with self.assertRaises(ValueError):
            pilot_timing_guard(m, (0.,), loss_budget_db=0.)

    def test_guard_helper_off_and_none_are_exact_bypasses(self):
        m, source = fixture(False)
        absent = pilot_timing_guard(m, (0.,))
        self.assertFalse(absent['guard_applied'])
        self.assertEqual(absent['reason'], 'helper_disabled')
        self.assertEqual(absent['effective_pilot_use'], 'both')
        self.assertIsNone(absent['allowed_cover_radius_seconds'])
        m['helper_enabled'] = True
        none = pilot_timing_guard(m, (0.,), pilot_use='none')
        self.assertFalse(none['guard_applied'])
        self.assertEqual(none['reason'], 'pilots_not_requested')

    def test_clock_and_motion_remain_in_discrete_arrival_guard(self):
        m, _ = fixture(True)
        m['channel']['sample_clock_pair_ppm'] = 1000.
        guard = pilot_timing_guard(m, (0.,), arrival_model='discrete')
        self.assertEqual(guard['arrival_timing_radius_seconds'], 0.)
        self.assertEqual(guard['clock_bank_cover_radius_ppm'], 1000.)
        self.assertGreater(guard['clock_timing_radius_seconds'], guard['allowed_cover_radius_seconds'])
        self.assertEqual(guard['effective_pilot_use'], 'none')
        # A real continuous clock interval still has interpolation uncertainty
        # even if both endpoint clock candidates are explicitly included.
        sampled = pilot_timing_guard(m, (0.,), arrival_model='discrete', clock_ppm=(-1000., 0., 1000.))
        self.assertEqual(sampled['clock_bank_cover_radius_ppm'], 500.)
        self.assertEqual(sampled['effective_pilot_use'], 'none')
        m['channel'].update(sample_clock_pair_ppm=0., delay_excursion_seconds=.001)
        motion = pilot_timing_guard(m, (0.,), arrival_model='discrete')
        self.assertEqual(motion['total_timing_radius_seconds'], .001)
        self.assertEqual(motion['effective_pilot_use'], 'none')

    def test_missing_uncertainty_metadata_does_not_become_zero(self):
        m, _ = base_fixture(True)
        guard = pilot_timing_guard(m, (0.,), arrival_model='discrete')
        self.assertEqual(guard['effective_pilot_use'], 'none')
        self.assertEqual(guard['reason'], 'helper_phase_timing_unresolved')
        self.assertIsNone(guard['total_timing_radius_seconds'])
        self.assertEqual(guard['missing_uncertainty_fields'], ['sample_clock_pair_ppm', 'delay_excursion_seconds'])


if __name__ == '__main__':
    unittest.main()

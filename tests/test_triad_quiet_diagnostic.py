"""Conditional quiet projection uses the production DFT and segment calendar."""
import math
from pathlib import Path
import sys
import unittest

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_quiet_diagnostic import QuietEnergyAccumulator, quiet_probability_bound
from triad_timing_forward import SparseTimingForward, TimingState
from triad_tracking_receiver import PracticalTrackingReceiver


class QuietDiagnosticTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)

    @staticmethod
    def receiver(values, *, aperture=(17, 48)):
        def basis(states, anchor, first, stop, origin, offsets):
            return np.zeros((3, len(states), 1, stop-first), dtype=np.complex128)
        prior=SparseTimingForward([TimingState(0,0,0)], model_count=1,
            delay_step_seconds=.1, rate_step_ppm=1, maximum_rate_ppm=0,
            maximum_acceleration_ppm_per_second=0, maximum_states=4, maximum_proposals=4)
        receiver=PracticalTrackingReceiver(sample_rate=16, samples=len(values), block_samples=8,
            segment_samples=16, basis_provider=basis, timing_forward=prior,
            required_bands=[[-7,7]], noise_variance=1.7, stationary_power=(.8,),
            coherence_seconds=(math.inf,), precision='fp64', projection_mode='dft',
            quiet_sample_bounds=aperture, memory_limit_bytes=256*1024**2)
        receiver.push(0, values)
        return receiver.finish()

    @staticmethod
    def accumulator(values, chunks, *, aperture=(17,48), valid=None):
        result=QuietEnergyAccumulator(samples=len(values), sample_rate=16,
            block_samples=8, segment_samples=16, quiet_sample_bounds=aperture,
            noise_variance=1.7, required_bands=[[-7,7]],
            memory_limit_bytes=2*1024**2, maximum_input_chunk_samples=64)
        for first in range(0, len(values), chunks):
            stop=min(first+chunks, len(values))
            result.push(first, values[first:stop], None if valid is None else valid[first:stop])
        return result.finish()

    def test_matches_actual_affine_lane_and_preserves_split_chunk_equivalence(self):
        rng=np.random.default_rng(213)
        values=rng.normal(size=53)+1j*rng.normal(size=53)
        actual=self.receiver(values)
        expected=actual['lanes']['initial_affine']
        for chunk in (1, 13, 64):
            result=self.accumulator(values,chunk)
            self.assertEqual(result['status'],'complete')
            self.assertEqual(result['projected_modes'],expected['quiet_observed_projection_modes'])
            self.assertAlmostEqual(result['projected_signal_energy']/1.7,
                expected['quiet_projected_energy'],places=11)
            self.assertEqual(result['conditional_data_to_quiet']['status'],
                'conditional_fixed_channel_bound')
        self.assertEqual(expected['quiet_observed_projection_modes'],24)

    def test_aperture_edges_and_empty_aperture_count_only_whole_valid_blocks(self):
        values=np.ones(53,dtype=np.complex128)
        clipped=self.accumulator(values,7,aperture=(25,41))
        empty=self.accumulator(values,11,aperture=(22,22))
        self.assertEqual(clipped['projected_modes'],8)
        self.assertEqual(empty['projected_modes'],0)
        self.assertEqual(empty['conditional_data_to_quiet']['status'],'unavailable')
        self.assertIsNone(empty['conditional_data_to_quiet']['bound'])

    def test_terminal_merged_block_matches_receiver_and_is_reserved(self):
        rng=np.random.default_rng(221)
        values=rng.normal(size=61)+1j*rng.normal(size=61)
        expected=self.receiver(values,aperture=(0,61))['lanes']['initial_affine']
        measured=self.accumulator(values,7,aperture=(0,61))
        self.assertEqual(measured['projected_modes'],expected['quiet_observed_projection_modes'])
        self.assertAlmostEqual(measured['projected_signal_energy']/1.7,
            expected['quiet_projected_energy'],places=11)
        self.assertGreaterEqual(measured['workspace_estimate_bytes'],64*(16+64)+192*13)

    def test_partial_or_erased_capture_never_yields_probability_bound(self):
        values=np.ones(53,dtype=np.complex128)
        partial=QuietEnergyAccumulator(samples=53,sample_rate=16,block_samples=8,
            segment_samples=16,quiet_sample_bounds=(0,53),noise_variance=1.7,
            required_bands=[[-7,7]],memory_limit_bytes=2*1024**2,maximum_input_chunk_samples=64)
        partial.push(0,values[:37])
        result=partial.finish()
        self.assertFalse(result['complete'])
        self.assertIsNone(result['conditional_data_to_quiet']['bound'])
        valid=np.ones(53,dtype=np.uint8);valid[28]=0
        erased=self.accumulator(values,9,valid=valid)
        self.assertFalse(erased['complete'])
        self.assertEqual(erased['invalid_rows'],1)
        self.assertIsNone(erased['conditional_data_to_quiet']['bound'])

    def test_rejects_bad_stream_and_preflights_segment_workspace(self):
        with self.assertRaises(MemoryError):
            QuietEnergyAccumulator(samples=10000,sample_rate=1000,block_samples=100,
                segment_samples=10000,quiet_sample_bounds=(0,10000),noise_variance=1.,
                required_bands=[[-100,100]],memory_limit_bytes=1024)
        values=np.ones(10,dtype=np.complex128)
        source=QuietEnergyAccumulator(samples=10,sample_rate=16,block_samples=8,
            segment_samples=8,quiet_sample_bounds=(0,10),noise_variance=1.,
            required_bands=[[-7,7]],memory_limit_bytes=2*1024**2,maximum_input_chunk_samples=8)
        with self.assertRaisesRegex(ValueError,'contiguous'):
            source.push(1,values[:4])
        with self.assertRaisesRegex(ValueError,'validity'):
            source.push(0,values[:4],np.ones(3,dtype=np.uint8))
        with self.assertRaisesRegex(ValueError,'finite'):
            source.push(0,np.full(4,np.nan+1j))

    def test_conditional_tail_bound_validation_and_snr_monotonicity(self):
        zero=quiet_probability_bound(0.,12,2.,alpha=.001)
        self.assertAlmostEqual(zero['bound'],.999,places=10)
        self.assertLess(quiet_probability_bound(100.,12,2.)['bound'],
                        quiet_probability_bound(10.,12,2.)['bound'])
        for args in ((-1.,12,2.),(1.,True,2.),(1.,12,0.),(1.,12,2.,0.)):
            with self.assertRaises(ValueError):
                if len(args)==4:quiet_probability_bound(*args[:3],alpha=args[3])
                else:quiet_probability_bound(*args)
        overflow=quiet_probability_bound(1e308,1,1e-308)
        self.assertEqual(overflow['status'],'unavailable')
        self.assertIsNone(overflow['bound'])
        self.assertEqual(overflow['reason'],'noncentrality_outside_finite_range')
        with self.assertRaises(ValueError):
            quiet_probability_bound(1.,1 << 64,1.)


if __name__ == '__main__':
    unittest.main()

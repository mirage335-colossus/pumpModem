"""Public-bank and single-stream accounting checks with real receiver kernels."""
from pathlib import Path
import math
import sys
import unittest
from unittest import mock

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from triad_case_receiver import EPOCHS, PreparedCase, _timing, _manifest


class PublicNativeFixture:
    """Tiny native-protocol stand-in; acquisition/tracking are not mocked."""
    instances = []

    def __init__(self, executable, arguments, *, kind):
        self.arguments = arguments
        self.next_sample = 0
        self.closed = False
        self.requests = []
        self.plan = dict(format=dict(elapsed_bit_seconds=16.,
                occupied_bandwidth_hz=60., fine_chip_rate_hz=10.,
                pulse_shape='rrc-025-span8-c2-tail2', fhss=False),
            geometry=dict(source_calendar_origin_sample=100),
            physics=dict(arrival_prior_seconds=[-.05, .05], spread_margin=1.,
                deterministic_cfo_bound_hz=.02, sample_clock_pair_ppm=0.,
                base_relative_pair_ppm_bound=0., spread_delay_ms=0.,
                spread_doppler_hz=0.))
        self.descriptor = dict(sample_rate_hz=1000, source_samples=16000,
            capture_lead_seconds=.1, data_offset_samples=1000, total_samples=20000,
            emitted_samples=17000, helper_enabled=False, epoch=1793404800,
            data_if_hz=100., helper_low_hz=None, helper_high_hz=None, noise_variance=.1,
            fixed_workspace_bytes=4096)
        self.instances.append(self)

    @staticmethod
    def pattern(coordinates, bit, epoch):
        t = coordinates / 1000
        frequency = (5. if bit == 0 else 27.) + epoch * .2
        return np.exp(2j * np.pi * (frequency * t + .3 * t * t))

    def next(self, count):
        first = self.next_sample
        self.requests.append((first, count))
        self.next_sample += count
        coordinates = np.arange(first, first + count) - 1100
        # Truth exists only inside the independent fixture, never in a public
        # descriptor supplied to the receiver's bank construction.
        return self.pattern(coordinates, 1, 0), np.ones(count, np.uint8)

    def basis(self, first, count, rows):
        return np.array([self.pattern(first + np.arange(count), row[0], row[1])
                         for row in rows], dtype=np.complex128)

    def basis_mapped(self, first, offsets, rows, center):
        t = (first + offsets) / 1000
        result = []
        for bit, epoch, arrival, frequency, clock, delay, doppler in rows:
            scale = 1 + clock * 1e-6
            u = (t - arrival - delay) * scale * 1000
            result.append(self.pattern(u, bit, epoch) *
                np.exp(2j * np.pi * (frequency + doppler) * t))
        return np.asarray(result)

    def close(self):
        self.closed = True


class CaseReceiverTests(unittest.TestCase):
    def test_helper_onset_uses_native_calendar_not_emission_origin(self):
        source = PublicNativeFixture('fixture', [], kind='channel')
        source.descriptor['helper_enabled'] = True
        source.plan['transmit_calendar'] = dict(first_helper_start_seconds=22.,
            first_helper_end_seconds=732.401)
        manifest = _manifest(source.plan, source.descriptor, 1000, 0, 1)
        self.assertEqual(manifest['calendar']['helper_start_seconds'], 22.)
        self.assertEqual(manifest['calendar']['helper_end_seconds'], 732.401)
        source.plan['format']['fhss'] = True
        manifest = _manifest(source.plan, source.descriptor, 1000, 0, 1)
        self.assertTrue(manifest['helper_plan']['pilot_dwell_gating'])
        from triad_weak_helper import estimate_weak_helper
        result = estimate_weak_helper(np.zeros(1000, dtype=complex), manifest, [0.])
        self.assertEqual(result['reason'], 'gated_or_retuned_pilots_unsupported')
        del source.plan['transmit_calendar']
        with self.assertRaisesRegex(ValueError, 'native public transmit calendar'):
            _manifest(source.plan, source.descriptor, 1000, 0, 1)

    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)

    def prepare(self, **kwargs):
        options = dict(measurement_seconds=.125, source_factory=PublicNativeFixture,
            memory_limit_bytes=32 * 1024**2, threads=1)
        options.update(kwargs)
        return PreparedCase('fixture', [], **options)

    def test_helper_frequency_horizon_covers_searched_observation(self):
        from triad_weak_helper import estimate_weak_helper
        with self.prepare() as prepared, mock.patch(
                'triad_case_receiver.estimate_weak_helper', wraps=estimate_weak_helper) as helper:
            prepared.measure(1)
            config = helper.call_args.kwargs['config']['frequency_config']
            self.assertEqual(config['selection_geometry'], 'continuous_interval')
            self.assertEqual(config['future_horizon_seconds'],
                (prepared.raw_first_sample+len(prepared.raw))/prepared.sample_rate)

    def test_actual_kernels_keep_public_bank_and_fresh_tracking_state(self):
        with self.prepare() as prepared:
            self.assertEqual(prepared.coverage['epochs'], list(EPOCHS))
            self.assertEqual(prepared.coverage['arrival_prior_seconds'], [-.05, .05])
            self.assertFalse(prepared.coverage['whole_bit_covered'])
            self.assertEqual(prepared.manifest['key_seed'], 1)
            one = prepared.measure(1)
            four = prepared.measure(4)
            again = prepared.measure(1)
            for result in (one, four, again):
                acquisition, tracking = result['stages'].values()
                self.assertTrue(acquisition['candidates'])
                self.assertTrue(tracking['complete_observation'])
                self.assertEqual(tracking['candidate_bit'], 1)
                self.assertIsNone(result['outcome']['whole_bit_success_probability'])
                self.assertFalse(result['outcome']['success_trials_independent'])
                self.assertTrue(tracking['native_template_rendering_timed'])
                self.assertTrue(tracking['representative_work'])
                self.assertNotIn('service_elapsed_seconds', tracking)
                self.assertNotIn('process_cpu_seconds', tracking)
            self.assertEqual(four['measured_baseband_seconds'], 4 * one['measured_baseband_seconds'])
            np.testing.assert_array_equal(one['stages']['tracking']['bit_scores'],
                                          again['stages']['tracking']['bit_scores'])
            self.assertGreater(four['stages']['tracking']['work_counts']['basis_rows'],
                               one['stages']['tracking']['work_counts']['basis_rows'])
            source = prepared.source
            self.assertEqual(source.requests[0][0], 0)
            self.assertEqual(sum(count for _, count in source.requests),
                prepared.raw_first_sample + len(prepared.raw))
        self.assertTrue(source.closed)
        with self.assertRaisesRegex(RuntimeError, 'closed'):
            prepared.measure()

    def test_timing_ratio_uses_wall_time_without_core_division(self):
        with mock.patch('triad_case_receiver.time.perf_counter', return_value=12.), \
             mock.patch('triad_case_receiver.time.process_time', return_value=15.):
            timing = _timing(10., 3., 20., 12)
        self.assertEqual(timing['ratio_processing_seconds_per_baseband_second'], .1)
        self.assertEqual(timing['observed_cpu_utilization_fraction'], .5)

    def test_zero_basis_is_not_representative_capacity_work(self):
        with self.prepare(template_backend='direct') as prepared:
            with mock.patch.object(prepared.source, 'basis_mapped', side_effect=lambda first, offsets, rows, center:
                    np.zeros((len(rows), len(offsets)), complex)):
                result = prepared.measure()['stages']['tracking']
            self.assertEqual(result['rendered_template_energy'], 0.)
            self.assertFalse(result['representative_work'])
            self.assertGreater(result['work_counts']['candidate_block_updates'], 0)

    def test_acquisition_guard_retains_explicit_diagnostic_subset(self):
        with self.prepare() as prepared:
            with mock.patch.object(prepared.screen, 'score', return_value=dict(
                    status='unresolved', reason='fft_product_work_limit', candidates=[])):
                result = prepared.measure()
            tracking = result['stages']['tracking']
            self.assertEqual(result['stages']['acquisition']['reason'], 'fft_product_work_limit')
            self.assertEqual(tracking['bank_source'],
                'public_diagnostic_subset_after_acquisition_unresolved')
            self.assertEqual({row[0] for row in tracking['selected_coordinates']}, set(EPOCHS))
            self.assertLess(tracking['selected_prior_mass'], 1.)
            self.assertTrue(tracking['complete_observation'])

    def test_setup_failure_closes_native_owner_and_rejects_ambiguous_arguments(self):
        with self.assertRaises(MemoryError):
            self.prepare(memory_limit_bytes=32)
        self.assertTrue(PublicNativeFixture.instances[-1].closed)
        for arguments in (['--seed', '1', '--seed', '2'], ['--output', 'capture']):
            with self.assertRaises(ValueError):
                PreparedCase('fixture', arguments, source_factory=PublicNativeFixture)
        with self.prepare() as prepared:
            for units in (0, 3, True):
                with self.assertRaises(ValueError):
                    prepared.measure(units)


if __name__ == '__main__':
    unittest.main()

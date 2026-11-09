"""Shared public-manifest and native-process accounting helper checks."""
from pathlib import Path
import os
import sys
import unittest
from types import SimpleNamespace

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from triad_case_receiver import EPOCHS, _manifest, _native_cpu_seconds, _option, _rss_snapshot


def public_plan():
    return dict(format=dict(elapsed_bit_seconds=16., fhss=False),
        physics=dict(arrival_prior_seconds=[-.05, .05], spread_delay_ms=0.,
            base_relative_pair_ppm_bound=0.))


def public_descriptor():
    return dict(sample_rate_hz=1000, emitted_samples=17000,
        capture_lead_seconds=.1, data_offset_samples=1000, helper_enabled=False,
        epoch=1793404800, data_if_hz=100., helper_low_hz=None, helper_high_hz=None,
        noise_variance=.1)


class SharedReceiverHelperTests(unittest.TestCase):
    def test_manifest_uses_public_helper_calendar_and_gates_fhss(self):
        plan, descriptor = public_plan(), public_descriptor()
        descriptor['helper_enabled'] = True
        plan['format']['fhss'] = True
        plan['transmit_calendar'] = dict(first_helper_start_seconds=22.,
            first_helper_end_seconds=732.401)
        manifest = _manifest(plan, descriptor, 1000, 7, 1)
        self.assertEqual(manifest['calendar']['helper_start_seconds'], 22.)
        self.assertEqual(manifest['calendar']['helper_end_seconds'], 732.401)
        self.assertEqual(manifest['calendar']['data_sample_offset'], 1007)
        self.assertTrue(manifest['helper_plan']['pilot_dwell_gating'])
        self.assertEqual(manifest['format']['bit_seconds'], 16.)
        self.assertEqual(EPOCHS, tuple(range(-6, 7)))
        from triad_weak_helper import estimate_weak_helper
        result = estimate_weak_helper(np.zeros(1000, dtype=complex), manifest, [0.])
        self.assertEqual(result['reason'], 'gated_or_retuned_pilots_unsupported')

    def test_manifest_requires_calendar_for_helper(self):
        descriptor = public_descriptor()
        descriptor['helper_enabled'] = True
        with self.assertRaisesRegex(ValueError, 'native public transmit calendar'):
            _manifest(public_plan(), descriptor, 1000, 0, 1)

    def test_option_replacement_is_unique_and_requires_existing_value(self):
        arguments = ['--seed', '2']
        _option(arguments, '--seed', 5)
        self.assertEqual(arguments, ['--seed', '5'])
        _option(arguments, '--key-seed', 9)
        self.assertEqual(arguments[-2:], ['--key-seed', '9'])
        for invalid in (['--seed', '1', '--seed', '2'], ['--seed']):
            with self.assertRaises(ValueError):
                _option(invalid, '--seed', 3)

    def test_process_accounting_handles_absent_or_unreadable_child(self):
        self.assertIsNone(_native_cpu_seconds(object()))
        source = SimpleNamespace(process=SimpleNamespace(pid=-1))
        self.assertIsNone(_native_cpu_seconds(source))
        rss = _rss_snapshot(source)
        self.assertIsInstance(rss['parent_process_bytes'], int)
        self.assertIsNone(rss['native_child_bytes'])
        self.assertIsNone(rss['sum_process_bytes'])
        self.assertIn('not exclusive DSP storage', rss['scope'])


if __name__ == '__main__':
    unittest.main()

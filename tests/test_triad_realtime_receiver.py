"""Adaptive-procedure accounting and admission contracts; no long campaign."""
import copy
from pathlib import Path
import sys
import unittest
from unittest import mock

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_triad_dynamic_acquire import fixture
from triad_realtime_receiver import (RealtimeReceiver, refinement_coordinates,
                                      freeze_gate, admit)


class RealtimeContracts(unittest.TestCase):
    def test_clock_refinement_preserves_pattern_midpoint(self):
        m, _ = fixture(False)
        m['calendar']['arrival_prior_seconds'] = [-1., 1.]
        m['calendar']['data_sample_offset'] = 30000
        m['format']['bit_seconds'] = 20.
        m['channel'].update(sample_clock_pair_ppm=100., spread_margin=3.)
        c = dict(epoch=0, arrival=.1, frequency=3., clock_ppm=0., bit=0)
        points = refinement_coordinates([c], m, clock_half_step_ppm=150,
            timing_step_seconds=0.)
        center = 30000/m['sample_rate_hz']+10
        self.assertEqual(len(points), 3)
        for _, arrival, frequency, q in points:
            self.assertAlmostEqual(arrival+center/(1+q*1e-6), .1+center, places=12)
            self.assertEqual(frequency, 3.)

    def test_sparse_budget_and_boundary(self):
        m, _ = fixture(False)
        m['format']['bit_seconds'] = 1.
        m['channel'].update(sample_clock_pair_ppm=100., spread_margin=3.)
        c = dict(epoch=0, arrival=m['calendar']['arrival_prior_seconds'][0],
                 frequency=0., clock_ppm=-300., bit=1)
        points = refinement_coordinates([c], m, clock_half_step_ppm=150,
            timing_step_seconds=.001)
        for _, a, _, q in points:
            self.assertGreaterEqual(a, m['calendar']['arrival_prior_seconds'][0])
            self.assertLessEqual(abs(q), 300)
        with self.assertRaisesRegex(ValueError, 'budget'):
            refinement_coordinates([c], m, clock_half_step_ppm=150,
                timing_step_seconds=.001, maximum_candidates=1)

    def test_rank_resolution_ties_and_unresolved_are_conservative(self):
        def row(score, status='scored'):
            return dict(procedure_id='frozen', status=status, score=score, bit=1,
                        reason='unresolved_helper')
        with self.assertRaisesRegex(ValueError, 'too few'):
            freeze_gate([row(0.)]*18, alpha=.05)
        gate = freeze_gate([row(1.)]*19, alpha=.05)
        self.assertFalse(admit(row(1.), gate)['accepted'])
        self.assertTrue(admit(row(2.), gate)['accepted'])
        self.assertFalse(admit(row(None, 'unresolved'), gate)['accepted'])
        gate = freeze_gate([row(None, 'unresolved')]*19, alpha=.05)
        self.assertEqual(admit(row(2.), gate)['p_value'], .05)
        changed = row(2.); changed['procedure_id'] = 'changed'
        with self.assertRaisesRegex(ValueError, 'mismatch'):
            admit(changed, gate)

    def test_pipeline_preserves_original_prior_and_stops_on_helper_failure(self):
        m, source = fixture(True)
        m['channel'].update(deterministic_cfo_bound_hz=5., spread_margin=1.,
                            sample_clock_pair_ppm=10.)
        m.update(schema='test', public_epoch_seconds=1000, key_seed=1)
        original = copy.deepcopy(m)
        fake = mock.Mock()
        fake.procedure_id = 'coarse-policy'
        with mock.patch('triad_realtime_receiver.FFTScreen', return_value=fake), \
             mock.patch('triad_realtime_receiver.estimate_helper',
                        return_value=dict(status='fallback', frequency=None)):
            receiver = RealtimeReceiver(m, source, [0])
            out = receiver.score(np.zeros(m['calendar']['capture_samples'], complex))
        self.assertEqual(out['status'], 'unresolved')
        self.assertIsNone(out['bit'])
        fake.score.assert_not_called()
        self.assertEqual(m, original)

    def test_present_absent_control_labels_do_not_change_procedure(self):
        m, source = fixture(True)
        m['channel'].update(deterministic_cfo_bound_hz=5., spread_margin=1.,
                            sample_clock_pair_ppm=10.)
        m.update(schema='test', public_epoch_seconds=1000, key_seed=1, data_enabled=True)
        with mock.patch('triad_realtime_receiver.FFTScreen') as screen:
            screen.return_value.procedure_id = 'coarse-policy'
            first = RealtimeReceiver(m, source, [0])
            m.update(data_enabled=False, evaluator_truth_file='DO_NOT_READ.json')
            second = RealtimeReceiver(m, source, [0])
        self.assertEqual(first.procedure_id, second.procedure_id)
        with mock.patch('triad_realtime_receiver.FFTScreen') as screen:
            screen.return_value.procedure_id = 'changed-coarse-policy'
            changed = RealtimeReceiver(m, source, [0])
        self.assertNotEqual(first.procedure_id, changed.procedure_id)

    def test_refinement_budget_returns_unresolved_without_scoring(self):
        m, source = fixture(True)
        m['channel'].update(deterministic_cfo_bound_hz=5., spread_margin=1.,
                            sample_clock_pair_ppm=10.)
        m['format']['bit_seconds'] = 1.
        m.update(schema='test', public_epoch_seconds=1000, key_seed=1)
        proposals = [dict(epoch=0, arrival=0., frequency=0., clock_ppm=0., bit=b)
                     for b in (0, 1)]
        screen = mock.Mock(procedure_id='coarse')
        screen.score.return_value = dict(candidates=proposals)
        helper = dict(frequency=dict(status='narrowed', expanded_intervals_hz=[[0., 1.]]))
        with mock.patch('triad_realtime_receiver.FFTScreen', return_value=screen), \
             mock.patch('triad_realtime_receiver.estimate_helper', return_value=helper), \
             mock.patch('triad_realtime_receiver.ScanJointReceiver') as dynamic:
            receiver = RealtimeReceiver(m, source, [0], maximum_refinement_candidates=1)
            out = receiver.score(np.zeros(m['calendar']['capture_samples'], complex))
        self.assertEqual(out['reason'], 'refinement_work_limit')
        self.assertEqual(out['status'], 'unresolved')
        dynamic.assert_not_called()


if __name__ == '__main__':
    unittest.main()

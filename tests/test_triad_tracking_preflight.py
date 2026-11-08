"""Bounded default tracking preflight and exact work/memory guards."""
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_continuous import tracking_preflight


class PreflightTests(unittest.TestCase):
    def plan(self, **kwargs):
        descriptor = kwargs.pop('descriptor', dict(nominal_triplet_bandwidth_hz=.0625,
            data_if_hz=.05, fine_chip_rate_hz=.05))
        return tracking_preflight(dict(coordinates=[[0, 0, 0, 0]]), descriptor,
            samples=kwargs.pop('samples', 256), sample_rate=.25,
            coherences=(math.inf,), template_backend='direct', **kwargs)

    def test_two_lanes_share_calendar_but_both_cost_work(self):
        report = self.plan()
        adaptive, nominal = (report['lanes'][name] for name in ('adaptive', 'initial_affine'))
        self.assertEqual(report['work']['sample_rows'], 256)
        self.assertEqual(nominal['basis_row_samples'], 512)
        for name in ('basis_row_samples', 'gaussian_updates', 'transition_edges'):
            self.assertEqual(report['work'][name], adaptive[name]+nominal[name])
        self.assertEqual(report['identity']['regime']['receiver'], 'PracticalTrackingReceiver')
        self.assertFalse(report['cpu_forecast']['available'])
        self.assertEqual(report['memory_forecast']['status'], 'within')

    def test_very_long_plan_keeps_same_workspace(self):
        small, long = self.plan(), self.plan(samples=1<<50)
        self.assertEqual(small['workspace'], long['workspace'])
        self.assertGreater(long['work']['segments'], 1<<40)
        with self.assertRaises(MemoryError):
            self.plan(memory_mib=1)



if __name__ == '__main__':
    unittest.main()

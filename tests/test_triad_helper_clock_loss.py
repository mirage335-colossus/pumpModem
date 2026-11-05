"""Focused finite clock/rate template-subspace diagnostic contracts."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_helper_clock_loss import PublicClockGridLoss, _best_retention, _candidate_points


class ClockGridLossTests(unittest.TestCase):
    def test_cell_midpoints_and_outer_endpoints_are_declared(self):
        points = _candidate_points((-300., 0., 300.), (.99985, 1., 1.00015))
        centers = [p for p in points if p[2] == "cell-midpoint"]
        corners = [p for p in points if p[2] == "outer-endpoint"]
        self.assertEqual(len(centers), 4)
        self.assertEqual(len(corners), 4)
        self.assertIn((150., 1.000075, "cell-midpoint"), points)
        with self.assertRaisesRegex(ValueError, "at least two"):
            _candidate_points((0.,), (1., 1.1))

    def test_identical_two_ray_subspaces_retain_both_directions(self):
        n = 96
        t = np.arange(n)
        columns = np.column_stack((np.exp(2j * np.pi * .07 * t),
                                   np.exp(2j * np.pi * .19 * t)))
        score, shift, centered, directions, residual = _best_retention(columns, columns.copy(), 2.)
        self.assertAlmostEqual(score, 1., places=10)
        self.assertAlmostEqual(centered, 1., places=10)
        self.assertAlmostEqual(shift, 0., places=4)
        self.assertAlmostEqual(residual, 0., places=4)
        self.assertEqual(len(directions), 2)
        self.assertTrue(all(abs(value - 1.) < 1e-10 for value in directions))

    def test_rank_deficient_candidate_reports_zero_lost_direction(self):
        t = np.arange(100)
        target = np.column_stack((np.exp(2j*np.pi*.07*t), np.exp(2j*np.pi*.19*t)))
        candidate = np.column_stack((np.exp(2j*np.pi*.07*t), np.exp(2j*np.pi*.07*t)))
        score, _, _, directions, _ = _best_retention(target, candidate, 1.)
        self.assertEqual(score, 0.)
        self.assertEqual(directions, [0., 0.])

    def test_small_public_fixture_reports_only_sampled_grid_claim(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            rate = 18000
            t = np.arange(800) / rate
            source = np.stack([
                np.stack([np.exp(2j*np.pi*55*t), np.exp(2j*np.pi*65*t)]),
            ]).astype("<c16")
            source.tofile(folder / "templates.c128")
            manifest = dict(schema="triad-helper-public-v1", sample_rate_hz=rate,
                templates=dict(enabled=True, file="templates.c128", shape=[1, 2, 800],
                               epoch_offsets_seconds=[0]),
                calendar=dict(capture_lead_seconds=.002, data_sample_offset=54,
                              arrival_prior_seconds=[-.0005, .0005]),
                channel=dict(spread_delay_ms=.05, spread_doppler_hz=10))
            (folder / "manifest.json").write_text(json.dumps(manifest))
            result = PublicClockGridLoss(folder, (-100., 100.), (.9999, 1.0001)).run()
            self.assertEqual(len(result["results"]), 10)
            self.assertIn("not a continuous-grid worst-case bound", result["limitations"][0])
            self.assertEqual(result["identity"]["templates_sha256"],
                             PublicClockGridLoss._sha256(folder / "templates.c128"))
            for row in result["results"]:
                self.assertGreaterEqual(row["minimum_principal_energy_retention"], 0.)
                self.assertLessEqual(row["minimum_principal_energy_retention"], 1.)
                self.assertEqual(len(row["principal_energy_retention_by_direction"]), 2)


if __name__ == "__main__":
    unittest.main()
